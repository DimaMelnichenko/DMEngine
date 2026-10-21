#include "CDLODTerrain.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include "Assets.h"
#include "ConstantBuffers.h"
#include "Logger\Logger.h"
#include "Properties\SettingsProperties.h"

using namespace DirectX;

namespace GS
{

CDLODTerrain::CDLODTerrain() :
	SceneObject( "CDLOD terrain" )
{
}

bool CDLODTerrain::initialize( const TerrainSettings& settings, TerrainPipeline& pipeline, TerrainPaintTool& paint )
{
	m_pipeline = &pipeline;
	m_paintTool = &paint;
	m_terrainId = settings.id;
	const uint32_t mapSize = pipeline.mapSize();

	// Квадродерево над картой: лист — gridDim текселей, корень покрывает всю карту
	m_quadtree.initialize( XMFLOAT2( 0.0f, 0.0f ), gridDim * pipeline.texelSize(), pipeline.worldSize() );

	if( !createShader() )
		return false;
	buildNodeBounds();

	m_patch.initialize( patchDim + 1, patchDim + 1 );
	m_patchBuffer.createBuffer( sizeof( Patch ), CDLODQuadtree::maxPatches, "Terrain patches" );
	m_layerConstants.createBuffer( sizeof( TerrainLayerConstants ), 1, "Terrain layer constants" );

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( CDLODBuffer ), m_constantBuffer ) )
		return false;

	m_properties.setName( "CDLOD terrain" );

	const float leaf = m_quadtree.nodeSize( 0 );
	auto prop = m_properties.insert( "LOD distance", leaf * 4.0f );
	prop->setLow( leaf );
	prop->setHigh( leaf * 64.0f );
	prop->setControlType( GUIControlType::SLIDER );
	prop->setCategory( "Geometry" );
	prop->setUnit( "m" )->setTooltip( "Range of the finest regular level (leaf nodes); each coarser level reaches twice as far, "
									  "the finer detail levels near the camera - half, a quarter..." );

	// Материал и сетка у камеры — по описанию полей строки Terrain (terrainSchema)
	addSettingProperties( m_properties, terrainSchema(), settings );
	static_assert( detailLevels - tileLevels == 4, "Finest mesh: values of terrainSchema" );
	// Слои материала, эрозия, правила слоёв — части окна конвейера; кисть — подокно «Paint»
	pipeline.attachWindow( m_properties );
	m_properties.addSubContainer( paint.properties() );

	m_properties.insert( "Wireframe", false )->setCategory( "Debug" );
	m_properties.insert( "Show LOD", false )->setCategory( "Debug" );
	// Вода симуляции (WaterSimulation, SLOT_WATER) поверх земли: синее — глубже, светлее — быстрее
	m_properties.insert( "Show water", false )->setCategory( "Debug" );
	m_properties.insert( "Show wind", false )->setCategory( "Debug" )->setTooltip( "Wind field on the ground: arrows - direction, their color - speed against the "
														   "wind of the level (blue calm, green even, red twice faster), white edge - up the slope, "
														   "black - down; hatching - gusty" );

	LOG( "CDLOD terrain: heightmap " + std::to_string( mapSize ) + ", LOD levels " + std::to_string( m_quadtree.levelCount() ) );

	m_initialized = true;

	return true;
}

bool CDLODTerrain::createShader()
{
	std::vector<VertexElement> layout = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 } };
	m_shader.setLayoutDesc( std::move( layout ) );

	if( !m_shader.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\cdlod.vs" ) ||
		!m_shader.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\terrain.ps" ) ||
		!m_shader.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\cdlod_lod.ps" ) )
	{
		LOG( "CDLOD terrain: shader compilation failed" );
		return false;
	}

	// Фазы: материал террейна, раскраска по уровням LOD, только глубина (тени)
	m_materialPhase = m_shader.createPhase( 0, 0 );
	m_lodPhase = m_shader.createPhase( 0, 1 );
	m_depthPhase = m_shader.createPhase( 0, -1 );
	if( m_materialPhase < 0 || m_lodPhase < 0 || m_depthPhase < 0 )
		return false;
	return true;
}

void CDLODTerrain::warmPipelines( const PassStates& states )
{
	if( !m_initialized )
		return;
	// Цвет: глубина с записью или проверка на равенство после depth prepass; prepass — только глубина; тени — состояние
	// прохода теней рендерера (растеризатор со смещением глубины солнца)
	m_shader.warmPipelines( { { RasterState::solid, DepthState::enabled, BlendState::opaque },
							  { RasterState::solid, DepthState::readOnlyEqual, BlendState::opaque },
							  { RasterState::wireframe, DepthState::enabled, BlendState::opaque },
							  { RasterState::wireframe, DepthState::readOnlyEqual, BlendState::opaque } }, states.scene,
							{ m_materialPhase, m_lodPhase } );
	m_shader.warmPipelines( { { RasterState::solid, DepthState::enabled, BlendState::opaque },
							  { RasterState::wireframe, DepthState::enabled, BlendState::opaque },
							  states.shadowDepth }, states.depthOnly, { m_depthPhase } );
}

void CDLODTerrain::buildNodeBounds()
{
	m_boundsRevision = m_pipeline->heightRevision();
	// Нормированные min / max высоты по всем текселям мипа, которые билинейно читают вершины прямоугольника
	// [x0, x1] × [z0, z1] (в долях стороны террейна), с запасом в тексель по краям.
	// Ось z мира идёт по текстуре снизу вверх: в шейдере v = 1 - z / worldSize
	const std::vector<TerrainPipeline::HeightMip>& mips = m_pipeline->heightMips();
	if( mips.empty() )
		return;
	const uint32_t lastMip = static_cast<uint32_t>( mips.size() ) - 1;
	auto mipBounds = [&mips]( uint32_t mip, float x0, float x1, float z0, float z1, XMFLOAT2& bounds )
	{
		const TerrainPipeline::HeightMip& mipImage = mips[mip];
		const int width = static_cast<int>( mipImage.size );
		const int height = static_cast<int>( mipImage.size );

		const int colMin = std::max( static_cast<int>( std::floor( x0 * width ) ) - 1, 0 );
		const int colMax = std::min( static_cast<int>( std::ceil( x1 * width ) ) + 1, width - 1 );
		const int rowMin = std::max( static_cast<int>( std::floor( ( 1.0f - z1 ) * height ) ) - 1, 0 );
		const int rowMax = std::min( static_cast<int>( std::ceil( ( 1.0f - z0 ) * height ) ) + 1, height - 1 );

		for( int row = rowMin; row <= rowMax; ++row )
		{
			const float* line = &mipImage.values[static_cast<size_t>( row ) * mipImage.size];
			for( int col = colMin; col <= colMax; ++col )
			{
				bounds.x = std::min( bounds.x, line[col] );
				bounds.y = std::max( bounds.y, line[col] );
			}
		}
	};

	const uint32_t levelCount = m_quadtree.levelCount();
	m_heightBounds.assign( levelCount, {} );
	m_heightExtent.assign( levelCount, 0.0f );

	for( uint32_t level = 0; level < levelCount; ++level )
	{
		const uint32_t count = m_quadtree.nodesPerSide( level );
		const float size = m_quadtree.nodeSize( level ) / m_pipeline->worldSize();
		m_heightBounds[level].resize( count * count );

		for( uint32_t z = 0; z < count; ++z )
		{
			for( uint32_t x = 0; x < count; ++x )
			{
				XMFLOAT2 bounds( FLT_MAX, -FLT_MAX );
				mipBounds( std::min( level, lastMip ), x * size, ( x + 1 ) * size, z * size, ( z + 1 ) * size, bounds );
				mipBounds( std::min( level + 1, lastMip ), x * size, ( x + 1 ) * size, z * size, ( z + 1 ) * size, bounds );
				m_heightBounds[level][z * count + x] = bounds;
				m_heightExtent[level] = std::max( m_heightExtent[level], bounds.y - bounds.x );
			}
		}
	}
}

void CDLODTerrain::update( const FrameContext& frame )
{
	if( !m_initialized )
		return;

	m_pipeline->setHeightMultiplier( m_properties["Height multiplier"].data<float>() );
	// Итоговую карту построили заново (перестройка русел) — границы узлов по ней
	if( m_boundsRevision != m_pipeline->heightRevision() )
		buildNodeBounds();

	// Дальности уровней: «LOD distance», но не меньше нижней границы без трещин — по перепаду высот узлов уровня
	// (множитель высоты — из окна); уровни мельче листа — «Finest mesh» и «Finest mesh radius», запас — дисплейсмент
	CDLODQuadtree::LodSettings lod;
	lod.lodDistance = m_properties["LOD distance"].data<float>();
	lod.meshLevels = tileLevels + static_cast<uint32_t>( std::clamp( m_properties["Finest mesh"].data<int32_t>(), 0,
																	static_cast<int32_t>( detailLevels - tileLevels ) ) );
	lod.finestRadius = std::max( m_properties["Finest mesh radius"].data<float>(), 0.0f );
	lod.detailMargin = m_pipeline->material().maxDisplacement();
	// С горами вокруг карты — их дальние уровни и перепады их узлов: таблица общая
	lod.coarseLevel = m_farLevel;
	lod.coarseRange = m_farRange;
	std::vector<float> extent( std::max( m_heightExtent.size(), m_farHeightExtent.size() ), 0.0f );
	for( size_t level = 0; level < m_heightExtent.size(); ++level )
		extent[level] = std::max( 0.0f, m_heightExtent[level] * m_pipeline->heightMultiplier() );
	for( size_t level = 0; level < m_farHeightExtent.size(); ++level )
		extent[level] = std::max( extent[level], m_farHeightExtent[level] );
	m_quadtree.computeLod( lod, extent );
}

void CDLODTerrain::setFarLevels( uint32_t level, float range, std::vector<float> heightExtent )
{
	m_farLevel = level;
	m_farRange = range;
	m_farHeightExtent = std::move( heightExtent );
}

void CDLODTerrain::compute( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	// Слои материала — ресурсы сцены (terrain_layers.sh): выбор слоёв и дисплейсмент у шейдеров террейна, расстановки и
	// частиц — по одним данным, смещение земли одно. Точка LOD — главного вида (у видов теней та же)
	const TerrainLayerConstants constants = layerConstants( frame.view );
	m_layerConstants.updateData( &constants, sizeof( constants ) );
	m_layerConstants.setToSlot( SLOT_TERRAIN_LAYER_CONSTANTS );
}

TerrainSettings CDLODTerrain::settings() const
{
	TerrainSettings settings;
	settings.id = m_terrainId;
	readSettingProperties( m_properties, terrainSchema(), settings );
	return settings;
}

TerrainLayerConstants CDLODTerrain::layerConstants( const RenderView& view ) const
{
	TerrainLayerConstants constants = {};
	std::copy( m_pipeline->material().layerScale().begin(), m_pipeline->material().layerScale().end(), constants.layerScale );
	std::copy( m_pipeline->material().layerDisplacement().begin(), m_pipeline->material().layerDisplacement().end(), constants.layerDisplacement );
	std::copy( m_pipeline->material().layerTint().begin(), m_pipeline->material().layerTint().end(), constants.layerTint );
	constants.lodOrigin = view.lodOrigin;
	constants.lodEyeHeight = view.lodEyeHeight;
	constants.worldSize = m_pipeline->worldSize();
	constants.triplanarSharpness = m_properties["Triplanar sharpness"].data<float>();
	constants.heightBlendDepth = m_properties["Height blend"].data<float>();
	constants.layerCount = m_pipeline->material().layerCount();
	constants.splatDetailTile = m_pipeline->detailSplat().tileSize();
	const XMFLOAT2 displacement = m_properties["Displacement distance"].data<XMFLOAT2>();
	constants.displacementEnd = m_pipeline->material().maxDisplacement() > 0.0f ? std::max( displacement.y, 0.0f ) : 0.0f;
	constants.displacementStart = std::min( std::max( displacement.x, 0.0f ), constants.displacementEnd );
	// Подстилка под кронами — вместо трав
	const int32_t floorLayer = m_pipeline->roleLayer( TerrainLayerRole::forestFloor );
	constants.forestFloorLayer = floorLayer >= 0 && floorLayer < static_cast<int32_t>( constants.layerCount ) ? floorLayer : -1;
	constants.forestFloorStrength = m_properties["Forest floor"].data<float>();
	constants.forestFloorEdge = m_properties["Forest floor edge"].data<float>();
	constants.heightMultiplier = m_pipeline->heightMultiplier();
	constants.heightOffset = m_pipeline->heightOffset();
	constants.detailTile = m_pipeline->detailTileSize();
	constants.microShadows = std::clamp( m_properties["Micro shadows"].data<float>(), 0.0f, 1.0f );
	constants.farTextureScale = m_properties["Far texture scale"].data<float>();
	const XMFLOAT2 farBlend = m_properties["Far blend"].data<XMFLOAT2>();
	constants.farBlendStart = farBlend.x;
	constants.farBlendEnd = std::max( farBlend.y, farBlend.x + 1.0f );
	// Берег озёр (terrain_layers.sh): запас берега и глубина мелководья слоёв «Shallows»
	constants.lakeFreeboard = m_pipeline->lakeFreeboard();
	constants.lakeShallowDepth = XMFLOAT2( m_pipeline->layerRules().lakeShallowDepth.from, m_pipeline->layerRules().lakeShallowDepth.to );
	constants.wetBankHeight = std::max( m_pipeline->layerRules().wetBankHeight, 0.0f );
	std::copy( m_pipeline->material().layerOcclusion().begin(), m_pipeline->material().layerOcclusion().end(), constants.layerOcclusion );
	const int32_t litterLayer = m_pipeline->roleLayer( TerrainLayerRole::forestFloorBroadleaf );
	constants.forestLitterLayer = litterLayer >= 0 && litterLayer < static_cast<int32_t>( constants.layerCount ) ? litterLayer : -1;
	const int32_t mossLayer = m_pipeline->roleLayer( TerrainLayerRole::forestMoss );
	constants.forestMossLayer = mossLayer >= 0 && mossLayer < static_cast<int32_t>( constants.layerCount ) ? mossLayer : -1;
	constants.forestMoss = m_properties["Forest moss"].data<float>();
	constants.forestMossDry = m_properties["Forest moss on dry"].data<float>();
	const int32_t wetLayer = m_pipeline->roleLayer( TerrainLayerRole::grassWet );
	if( wetLayer >= 0 )
		constants.forestFloorWet = 1u << wetLayer;
	for( const TerrainLayerRole role : { TerrainLayerRole::grassDry, TerrainLayerRole::grassWet } )
	{
		const int32_t layer = m_pipeline->roleLayer( role );
		if( layer >= 0 && layer != constants.forestFloorLayer )
			constants.forestFloorReplaced |= 1u << layer;
	}
	return constants;
}

CDLODQuadtree::Box CDLODTerrain::node( uint32_t level, uint32_t x, uint32_t z ) const
{
	const float size = m_quadtree.nodeSize( level );
	const XMFLOAT2& bounds = m_heightBounds[level][z * m_quadtree.nodesPerSide( level ) + x];

	// С запасом на дисплейсмент (± половина наибольшего размаха): frustum и дальности не обрезают поднятые камни
	const float margin = 0.5f * m_pipeline->material().maxDisplacement();
	Box box;
	box.min = XMFLOAT3( x * size, bounds.x * m_pipeline->heightMultiplier() + m_pipeline->heightOffset() - margin, z * size );
	box.max = XMFLOAT3( std::min( ( x + 1 ) * size, m_pipeline->worldSize() ),
						bounds.y * m_pipeline->heightMultiplier() + m_pipeline->heightOffset() + margin,
						std::min( ( z + 1 ) * size, m_pipeline->worldSize() ) );
	return box;
}

CDLODQuadtree::Box CDLODTerrain::detailNode( uint32_t depth, uint32_t nx, uint32_t nz ) const
{
	// Высоты узла — его листа и детальной плитки листа, если она есть
	const uint32_t leafX = nx >> depth;
	const uint32_t leafZ = nz >> depth;
	XMFLOAT2 bounds = m_heightBounds[0][leafZ * m_quadtree.nodesPerSide( 0 ) + leafX];
	if( const XMFLOAT2* detail = m_pipeline->detailBounds( leafX, leafZ ) )
		bounds = XMFLOAT2( std::min( bounds.x, detail->x ), std::max( bounds.y, detail->y ) );
	const float size = m_quadtree.nodeSize( 0 ) / static_cast<float>( 1u << depth );
	const float margin = 0.5f * m_pipeline->material().maxDisplacement();
	Box box;
	box.min = XMFLOAT3( nx * size, bounds.x * m_pipeline->heightMultiplier() + m_pipeline->heightOffset() - margin, nz * size );
	box.max = XMFLOAT3( std::min( ( nx + 1 ) * size, m_pipeline->worldSize() ),
						bounds.y * m_pipeline->heightMultiplier() + m_pipeline->heightOffset() + margin,
						std::min( ( nz + 1 ) * size, m_pipeline->worldSize() ) );
	return box;
}

void CDLODTerrain::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	std::vector<Patch>& patches = m_patches[view.index];
	patches.clear();
	// В дальнем каскаде теней рельефа нет: его тень дальше каскадов — по карте края тени (TerrainSunShadow)
	if( !m_initialized || view.kind == RenderView::Kind::farShadow || m_heightBounds.empty() )
		return;

	m_quadtree.select( view, *this, patches );

	if( !patches.empty() )
		collector.addCustom( passBit( MeshPass::depthPrepass ) | passBit( MeshPass::opaque ) | passBit( MeshPass::csmShadowDepth ) );
}

void CDLODTerrain::renderCustom( const RenderContext& context )
{
	// В проходах только глубины (тени, depth prepass) — без раскраски LOD, материала и ресурсов пиксельного шейдера.
	// Каркас террейна — и в prepass: иначе сплошная глубина закрыла бы то, что видно сквозь каркас
	const bool depthOnly = isDepthOnlyPass( context.pass );
	std::vector<Patch>& patches = m_patches[context.view.index];

	ScopedRenderState terrainState;
	if( context.pass != MeshPass::csmShadowDepth && m_properties["Wireframe"].data<bool>() )
	{
		DMD3D::instance().setState( RasterState::wireframe );
	}

	// Цель кисти поверх земли: слой, мокрость или маска (её текстура хранилища — t7); нет её — подсветка маски (места звука)
	const ShaderView* paintMask = nullptr;
	int32_t paintPreview = depthOnly ? -1 : m_paintTool->previewTarget( paintMask );
	XMFLOAT3 previewColor = paintColor;
	float previewStrength = paintStrength;
	if( !depthOnly && paintPreview < 0 && m_highlightMask )
	{
		paintPreview = static_cast<int32_t>( TerrainMaterial::maxLayers ) + 1;
		paintMask = m_highlightMask;
		previewColor = m_highlightColor;
		previewStrength = highlightStrength;
	}

	Device::updateResource<CDLODBuffer>( m_constantBuffer, [this, paintPreview, previewColor, previewStrength, &context]( CDLODBuffer& params )
	{
		fillConstants( params, context.view, paintPreview, previewColor, previewStrength );
	} );
	DMD3D::instance().setConstantBuffer( SLOT_CB_MATERIAL, m_constantBuffer );

	m_patchBuffer.updateData( patches.data(), sizeof( Patch ) * patches.size() );
	m_patchBuffer.setToSlot( SLOT_INSTANCE_DATA );

	// Карта высот — вершинному шейдеру и пиксельному: по ней считается нормаль рельефа
	DMD3D::instance().setSRV( 0, m_pipeline->heightMapView() );
	// Слои материала (splat-карта, массивы текстур, детальный слой весов) — ресурсы сцены: их читает и дисплейсмент
	// вершинного шейдера в проходах только глубины, и модели, переходящие в землю
	if( !depthOnly && paintMask )
		DMD3D::instance().setSRV( 7, *paintMask );

	context.constants.setPerObjectBuffer( XMMatrixIdentity() );

	DMD3D::instance().setVertexBuffer( m_patch.vertexBuffer(), sizeof( XMFLOAT3 ) );
	DMD3D::instance().setIndexBuffer( m_patch.indexBuffer(), DXGI_FORMAT_R32_UINT );

	m_shader.setPass( depthOnly ? m_depthPhase : m_properties["Show LOD"].data<bool>() ? m_lodPhase : m_materialPhase );
	DMD3D::instance().drawIndexedInstanced( m_patch.indexCount(), static_cast<uint32_t>( patches.size() ), 0, 0 );
}

void CDLODTerrain::fillConstants( CDLODBuffer& params, const RenderView& view, int32_t paintPreview, const XMFLOAT3& previewColor,
								  float previewStrength ) const
{
	params.terrainLayers = layerConstants( view );
	params.worldSize = m_pipeline->worldSize();
	params.heightMultiplier = m_pipeline->heightMultiplier();
	params.heightOffset = m_pipeline->heightOffset();
	params.gridDim = static_cast<float>( patchDim );
	const CDLODQuadtree::Lod& lod = m_quadtree.lod();
	std::copy( std::begin( lod.morph ), std::end( lod.morph ), params.morphConsts );
	params.texelSize = m_pipeline->texelSize();
	params.showWater = m_properties["Show water"].data<bool>() ? 1 : 0;
	params.showWind = m_properties["Show wind"].data<bool>() && m_pipeline->hasWindField() ? 1 : 0;
	params.detailTile = m_pipeline->detailTileSize();
	params.wetnessBaked = m_pipeline->staticWater() && m_pipeline->detailSplat().tileSize() > 0.0f ? 1 : 0;
	params.wetDarkening = std::clamp( m_properties["Wet darkening"].data<float>(), 0.0f, 1.0f );
	params.paintPreview = paintPreview;
	params.paintPreviewColor = previewColor;
	params.paintPreviewStrength = previewStrength;
	params.waterBedLayer = m_pipeline->staticWater() && m_pipeline->channelLayer() < static_cast<int32_t>( m_pipeline->material().layerCount() ) ? m_pipeline->channelLayer() : -1;
	// Наносы на дне нижних озёр — те же правила, что у детального слоя весов (TerrainDetailSplat): уровень — в м мира
	params.lakeGravelLayer = params.waterBedLayer >= 0 ? m_pipeline->roleLayer( TerrainLayerRole::lakeGravel ) : -1;
	params.lakeSiltLayer = params.waterBedLayer >= 0 ? m_pipeline->roleLayer( TerrainLayerRole::lakeSilt ) : -1;
	params.lakeBedDepths = XMFLOAT4( m_pipeline->layerRules().lakeGravelDepth.from, m_pipeline->layerRules().lakeGravelDepth.to,
									 m_pipeline->layerRules().lakeSiltDepth.from, m_pipeline->layerRules().lakeSiltDepth.to );
	params.lakeSedimentLevel = XMFLOAT2( m_pipeline->heightOffset() + m_pipeline->layerRules().lakeSedimentHeight.from * m_pipeline->heightMultiplier(),
										 m_pipeline->heightOffset() + m_pipeline->layerRules().lakeSedimentHeight.to * m_pipeline->heightMultiplier() );
	std::copy( std::begin( lod.detailMorph ), std::end( lod.detailMorph ), params.detailMorph );
}

PropertyContainer* CDLODTerrain::properties()
{
	return &m_properties;
}

}
