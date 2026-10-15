#include "CDLODTerrain.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include "System.h"
#include "ConstantBuffers.h"
#include "Logger\Logger.h"
#include "Properties\SettingsProperties.h"

using namespace DirectX;

namespace
{

// Узел ближе диапазона LOD: расстояние от отрезка «глаза — ноги» (origin и на eyeHeight ниже; в полёте — точка) до коробки
// узла не больше radius — как lodDistance в cdlod.vs
bool lodRangeHitsBox( const XMFLOAT3& origin, float eyeHeight, float radius, const XMFLOAT3& boxMin, const XMFLOAT3& boxMax )
{
	float dx = std::max( { boxMin.x - origin.x, 0.0f, origin.x - boxMax.x } );
	float dy = std::max( { boxMin.y - origin.y, 0.0f, origin.y - eyeHeight - boxMax.y } );
	float dz = std::max( { boxMin.z - origin.z, 0.0f, origin.z - boxMax.z } );
	return dx * dx + dy * dy + dz * dz <= radius * radius;
}

}

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

	// Уровней столько, чтобы корень покрыл всю карту высот
	m_levelCount = 1;
	while( m_levelCount < maxLevels && ( gridDim << ( m_levelCount - 1 ) ) < mapSize )
		++m_levelCount;

	m_nodesPerSide.resize( m_levelCount );
	for( uint32_t level = 0; level < m_levelCount; ++level )
		m_nodesPerSide[level] = static_cast<uint32_t>( std::ceil( pipeline.worldSize() / nodeSize( level ) ) );

	if( !createShader() )
		return false;
	buildNodeBounds();

	m_patch.initialize( patchDim + 1, patchDim + 1 );
	m_patchBuffer.createBuffer( sizeof( PatchInstance ), maxPatches, "Terrain patches" );
	m_layerConstants.createBuffer( sizeof( LayerConstants ), 1, "Terrain layer constants" );

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) )
		return false;

	m_properties.setName( "CDLOD terrain" );

	auto prop = m_properties.insert( "LOD distance", nodeSize( 0 ) * 4.0f );
	prop->setLow( nodeSize( 0 ) );
	prop->setHigh( nodeSize( 0 ) * 64.0f );
	prop->setControlType( GUIControlType::SLIDER );
	prop->setUnit( "m" )->setTooltip( "Range of the finest regular level (leaf nodes); each coarser level reaches twice as far, "
									  "the finer detail levels near the camera - half, a quarter..." );

	// Материал и сетка у камеры — по описанию полей строки Terrain (terrainSchema)
	addSettingProperties( m_properties, terrainSchema(), settings );
	static_assert( detailLevels - tileLevels == 4, "Finest mesh: values of terrainSchema" );
	// Слои материала, эрозия, правила слоёв — части окна конвейера; кисть — подокно «Paint»
	pipeline.attachWindow( m_properties );
	m_properties.addSubContainer( paint.properties() );

	m_properties.insert( "Wireframe", false );
	m_properties.insert( "Show LOD", false );
	// Вода симуляции (WaterSimulation, SLOT_WATER) поверх земли: синее — глубже, светлее — быстрее
	m_properties.insert( "Show water", false );
	m_properties.insert( "Show wind", false )->setTooltip( "Wind field on the ground: arrows - direction, their color - speed against the "
														   "wind of the level (blue calm, green even, red twice faster), white edge - up the slope, "
														   "black - down; hatching - gusty" );

	LOG( "CDLOD terrain: heightmap " + std::to_string( mapSize ) + ", LOD levels " + std::to_string( m_levelCount ) );

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

	m_heightBounds.assign( m_levelCount, {} );

	for( uint32_t level = 0; level < m_levelCount; ++level )
	{
		const uint32_t count = m_nodesPerSide[level];
		const float size = nodeSize( level ) / m_pipeline->worldSize();
		m_heightBounds[level].resize( count * count );

		for( uint32_t z = 0; z < count; ++z )
		{
			for( uint32_t x = 0; x < count; ++x )
			{
				XMFLOAT2 bounds( FLT_MAX, -FLT_MAX );
				mipBounds( std::min( level, lastMip ), x * size, ( x + 1 ) * size, z * size, ( z + 1 ) * size, bounds );
				mipBounds( std::min( level + 1, lastMip ), x * size, ( x + 1 ) * size, z * size, ( z + 1 ) * size, bounds );
				m_heightBounds[level][z * count + x] = bounds;
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
	m_meshLevels = tileLevels + static_cast<uint32_t>( std::clamp( m_properties["Finest mesh"].data<int32_t>(), 0,
																  static_cast<int32_t>( detailLevels - tileLevels ) ) );
	calcRanges();
}

void CDLODTerrain::compute( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	// Слои материала — ресурсы сцены (terrain_layers.sh): выбор слоёв и дисплейсмент у шейдеров террейна, расстановки и
	// частиц — по одним данным, смещение земли одно. Точка LOD — главного вида (у видов теней та же)
	const LayerConstants constants = layerConstants( frame.view );
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

void CDLODTerrain::calcRanges()
{
	// Трещин нет, если на границе уровней L и L + 1 вершины уровня L уже полностью морфированы, а уровня L + 1 ещё
	// не начали. Граница удалена от камеры не больше чем на диапазон L плюс диагональ узла L (с перепадом высот),
	// а морфинг уровня L + 1 начинается через morphStartRatio диапазона L после конца уровня L. Отсюда нижняя граница
	// диапазона: диагональ / morphStartRatio. Меньшее значение "LOD distance" не применяется
	float minRange = 0.0f;
	for( uint32_t level = 0; level < m_levelCount; ++level )
	{
		float heightExtent = 0.0f;
		for( const XMFLOAT2& bounds : m_heightBounds[level] )
			heightExtent = std::max( heightExtent, ( bounds.y - bounds.x ) * m_pipeline->heightMultiplier() );

		const float size = nodeSize( level );
		const float diagonal = std::sqrt( 2.0f * size * size + heightExtent * heightExtent );
		minRange = std::max( minRange, diagonal / morphStartRatio / static_cast<float>( 1u << level ) );
	}

	float range = std::max( m_properties["LOD distance"].data<float>(), minRange );
	float previousRange = 0.0f;
	for( uint32_t level = 0; level < m_levelCount; ++level )
	{
		const float morphStart = previousRange + ( range - previousRange ) * morphStartRatio;
		m_ranges[level] = range;
		m_morphConsts[level] = XMFLOAT4( morphStart, 1.0f / ( range - morphStart ), 0.0f, 0.0f );

		previousRange = range;
		range *= 2.0f;
	}

	// Уровни мельче листа — от самого мелкого из рисуемых (m_meshLevels) к −1: диапазон −1 — половина диапазона листа, каждый
	// следующий — вдвое меньше (при «LOD distance» 128 м — 64, 32 … 2 м); самый мелкий полон в «Finest mesh radius» (вокруг ног
	// при ходьбе), его диапазон — не меньше радиуса / morphStartRatio. Без трещин (как выше): граница уровня с более крупным —
	// не дальше его диапазона и диагонали узла (склон до 45° и запас на дисплейсмент), морфинг крупного начинается через
	// morphStartRatio разницы диапазонов — она не меньше диагонали / morphStartRatio. Морфинг листа — дальше 0,66 его
	// диапазона, за диапазоном −1
	const float finestRadius = std::max( m_properties["Finest mesh radius"].data<float>(), 0.0f );
	const float margin = m_pipeline->material().maxDisplacement();
	float detailPrevious = 0.0f;
	float previousDiagonal = 0.0f;
	for( uint32_t depth = detailLevels; depth >= 1; --depth )
	{
		if( depth > m_meshLevels )
		{
			m_detailRanges[depth - 1] = 0.0f;	// не рисуется
			m_detailMorph[depth - 1] = XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
			continue;
		}
		float rangeAt = m_ranges[0] / static_cast<float>( 1u << depth );
		if( depth == m_meshLevels )
			rangeAt = std::max( rangeAt, finestRadius / morphStartRatio );
		rangeAt = std::min( std::max( rangeAt, detailPrevious + previousDiagonal / morphStartRatio ), m_ranges[0] * 0.5f );
		const float morphStart = detailPrevious + ( rangeAt - detailPrevious ) * morphStartRatio;
		m_detailRanges[depth - 1] = rangeAt;
		m_detailMorph[depth - 1] = XMFLOAT4( morphStart, 1.0f / std::max( rangeAt - morphStart, 1e-3f ), 0.0f, 0.0f );
		detailPrevious = rangeAt;
		const float size = nodeSize( 0 ) / static_cast<float>( 1u << depth );
		previousDiagonal = std::sqrt( 2.0f * size * size + ( size + margin ) * ( size + margin ) );
	}
}

CDLODTerrain::LayerConstants CDLODTerrain::layerConstants( const RenderView& view ) const
{
	LayerConstants constants = {};
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
	return constants;
}

float CDLODTerrain::nodeSize( uint32_t level ) const
{
	return gridDim * m_pipeline->texelSize() * static_cast<float>( 1u << level );
}

CDLODTerrain::NodeBox CDLODTerrain::nodeBox( uint32_t level, uint32_t x, uint32_t z ) const
{
	const float size = nodeSize( level );
	const XMFLOAT2& bounds = m_heightBounds[level][z * m_nodesPerSide[level] + x];

	// С запасом на дисплейсмент (± половина наибольшего размаха): frustum и дальности не обрезают поднятые камни
	const float margin = 0.5f * m_pipeline->material().maxDisplacement();
	NodeBox box;
	box.min = XMFLOAT3( x * size, bounds.x * m_pipeline->heightMultiplier() + m_pipeline->heightOffset() - margin, z * size );
	box.max = XMFLOAT3( std::min( ( x + 1 ) * size, m_pipeline->worldSize() ),
						bounds.y * m_pipeline->heightMultiplier() + m_pipeline->heightOffset() + margin,
						std::min( ( z + 1 ) * size, m_pipeline->worldSize() ) );
	return box;
}

void CDLODTerrain::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	std::vector<PatchInstance>& patches = m_patches[view.index];
	patches.clear();
	// В дальнем каскаде теней рельефа нет: его тень дальше каскадов — по карте края тени (TerrainSunShadow)
	if( !m_initialized || view.farShadow )
		return;

	const uint32_t top = m_levelCount - 1;
	for( uint32_t z = 0; z < m_nodesPerSide[top]; ++z )
	{
		for( uint32_t x = 0; x < m_nodesPerSide[top]; ++x )
		{
			selectNode( view, top, x, z, patches );
		}
	}

	if( !patches.empty() )
		collector.addCustom( passBit( MeshPass::depthPrepass ) | passBit( MeshPass::opaque ) | passBit( MeshPass::csmShadowDepth ) );
}

bool CDLODTerrain::selectNode( const RenderView& view, uint32_t level, uint32_t x, uint32_t z,
							   std::vector<PatchInstance>& patches )
{
	const NodeBox box = nodeBox( level, x, z );
	const XMFLOAT3& cameraPosition = view.lodOrigin;
	const float eyeHeight = view.lodEyeHeight;

	if( !lodRangeHitsBox( cameraPosition, eyeHeight, m_ranges[level], box.min, box.max ) )
		return false;

	// Невидимый узел считается обработанным: рисовать его не нужно ни ему, ни родителю
	if( !view.frustum.checkBox( box.min, box.max ) )
		return true;

	// Лист у камеры — уровни мельче листа (−1, −2) везде: квадродерево однородно, переходы без трещин
	if( level == 0 && lodRangeHitsBox( cameraPosition, eyeHeight, m_detailRanges[0], box.min, box.max ) )
	{
		for( uint32_t quarter = 0; quarter < 4; ++quarter )
			if( !selectDetailNode( view, 1, x * 2 + ( quarter & 1 ), z * 2 + ( quarter >> 1 ), patches ) )
				addPatch( 0, x, z, quarter, patches );
		return true;
	}
	if( level == 0 || !lodRangeHitsBox( cameraPosition, eyeHeight, m_ranges[level - 1], box.min, box.max ) )
	{
		for( uint32_t quarter = 0; quarter < 4; ++quarter )
			addPatch( level, x, z, quarter, patches );
		return true;
	}

	// Четверти, которые дочерний узел не взял (он дальше своего диапазона), рисуются с детализацией этого уровня
	const uint32_t childCount = m_nodesPerSide[level - 1];
	for( uint32_t quarter = 0; quarter < 4; ++quarter )
	{
		const uint32_t cx = x * 2 + ( quarter & 1 );
		const uint32_t cz = z * 2 + ( quarter >> 1 );
		if( cx >= childCount || cz >= childCount )
			continue;

		if( !selectNode( view, level - 1, cx, cz, patches ) )
			addPatch( level, x, z, quarter, patches );
	}

	return true;
}

void CDLODTerrain::addPatch( uint32_t level, uint32_t x, uint32_t z, uint32_t quarter, std::vector<PatchInstance>& patches )
{
	const float size = nodeSize( level );
	const float halfSize = size * 0.5f;
	const XMFLOAT2 origin( x * size + ( quarter & 1 ) * halfSize, z * size + ( quarter >> 1 ) * halfSize );

	if( origin.x >= m_pipeline->worldSize() || origin.y >= m_pipeline->worldSize() || patches.size() >= maxPatches )
		return;

	patches.push_back( { origin, halfSize, static_cast<float>( level ) } );
}

bool CDLODTerrain::selectDetailNode( const RenderView& view, uint32_t depth, uint32_t nx, uint32_t nz, std::vector<PatchInstance>& patches )
{
	// Высоты узла — его листа и детальной плитки листа, если она есть
	const uint32_t leafX = nx >> depth;
	const uint32_t leafZ = nz >> depth;
	if( leafX >= m_nodesPerSide[0] || leafZ >= m_nodesPerSide[0] )
		return true;
	XMFLOAT2 bounds = m_heightBounds[0][leafZ * m_nodesPerSide[0] + leafX];
	if( const XMFLOAT2* detail = m_pipeline->detailBounds( leafX, leafZ ) )
		bounds = XMFLOAT2( std::min( bounds.x, detail->x ), std::max( bounds.y, detail->y ) );
	const float size = nodeSize( 0 ) / static_cast<float>( 1u << depth );
	const float margin = 0.5f * m_pipeline->material().maxDisplacement();
	const XMFLOAT3 boxMin( nx * size, bounds.x * m_pipeline->heightMultiplier() + m_pipeline->heightOffset() - margin, nz * size );
	const XMFLOAT3 boxMax( std::min( ( nx + 1 ) * size, m_pipeline->worldSize() ), bounds.y * m_pipeline->heightMultiplier() + m_pipeline->heightOffset() + margin,
						   std::min( ( nz + 1 ) * size, m_pipeline->worldSize() ) );
	const XMFLOAT3& origin = view.lodOrigin;
	if( !lodRangeHitsBox( origin, view.lodEyeHeight, m_detailRanges[depth - 1], boxMin, boxMax ) )
		return false;
	if( !view.frustum.checkBox( boxMin, boxMax ) )
		return true;
	if( depth >= m_meshLevels || !lodRangeHitsBox( origin, view.lodEyeHeight, m_detailRanges[depth], boxMin, boxMax ) )
	{
		for( uint32_t quarter = 0; quarter < 4; ++quarter )
			addDetailPatch( depth, nx, nz, quarter, patches );
		return true;
	}
	for( uint32_t quarter = 0; quarter < 4; ++quarter )
		if( !selectDetailNode( view, depth + 1, nx * 2 + ( quarter & 1 ), nz * 2 + ( quarter >> 1 ), patches ) )
			addDetailPatch( depth, nx, nz, quarter, patches );
	return true;
}

void CDLODTerrain::addDetailPatch( uint32_t depth, uint32_t nx, uint32_t nz, uint32_t quarter, std::vector<PatchInstance>& patches )
{
	const float size = nodeSize( 0 ) / static_cast<float>( 1u << depth );
	const float halfSize = size * 0.5f;
	const XMFLOAT2 origin( nx * size + ( quarter & 1 ) * halfSize, nz * size + ( quarter >> 1 ) * halfSize );
	if( origin.x >= m_pipeline->worldSize() || origin.y >= m_pipeline->worldSize() || patches.size() >= maxPatches )
		return;
	patches.push_back( { origin, halfSize, -static_cast<float>( depth ) } );
}

void CDLODTerrain::renderCustom( const RenderContext& context )
{
	// В проходах только глубины (тени, depth prepass) — без раскраски LOD, материала и ресурсов пиксельного шейдера.
	// Каркас террейна — и в prepass: иначе сплошная глубина закрыла бы то, что видно сквозь каркас
	const bool depthOnly = isDepthOnlyPass( context.pass );
	std::vector<PatchInstance>& patches = m_patches[context.view.index];

	ScopedRenderState terrainState;
	if( context.pass != MeshPass::csmShadowDepth && m_properties["Wireframe"].data<bool>() )
	{
		DMD3D::instance().setState( RasterState::wireframe );
	}

	// Цель кисти поверх земли: слой, мокрость или маска (её текстура хранилища — t7)
	const ShaderView* paintMask = nullptr;
	const int32_t paintPreview = depthOnly ? -1 : m_paintTool->previewTarget( paintMask );

	const LayerConstants layers = layerConstants( context.view );
	Device::updateResource<Parameters>( m_constantBuffer, [this, paintPreview, &layers]( Parameters& params )
	{
		params.layers = layers;
		params.worldSize = m_pipeline->worldSize();
		params.heightMultiplier = m_pipeline->heightMultiplier();
		params.heightOffset = m_pipeline->heightOffset();
		params.gridDim = static_cast<float>( patchDim );
		std::copy( std::begin( m_morphConsts ), std::end( m_morphConsts ), params.morphConsts );
		params.texelSize = m_pipeline->texelSize();
		params.farTextureScale = m_properties["Far texture scale"].data<float>();
		const XMFLOAT2 farBlend = m_properties["Far blend"].data<XMFLOAT2>();
		params.farBlendStart = farBlend.x;
		params.farBlendEnd = std::max( farBlend.y, farBlend.x + 1.0f );
		params.showWater = m_properties["Show water"].data<bool>() ? 1 : 0;
		params.showWind = m_properties["Show wind"].data<bool>() && m_pipeline->hasWindField() ? 1 : 0;
		params.detailTile = m_pipeline->detailTileSize();
		params.wetnessBaked = m_pipeline->staticWater() && m_pipeline->detailSplat().tileSize() > 0.0f ? 1 : 0;
		params.paintPreview = paintPreview;
		params.microShadows = std::clamp( m_properties["Micro shadows"].data<float>(), 0.0f, 1.0f );
		std::copy( m_pipeline->material().layerOcclusion().begin(), m_pipeline->material().layerOcclusion().end(), params.layerOcclusion );
		params.waterBedLayer = m_pipeline->staticWater() && m_pipeline->channelLayer() < static_cast<int32_t>( m_pipeline->material().layerCount() ) ? m_pipeline->channelLayer() : -1;
		// Наносы на дне нижних озёр — те же правила, что у детального слоя весов (TerrainDetailSplat): уровень — в м мира
		params.lakeGravelLayer = params.waterBedLayer >= 0 ? m_pipeline->roleLayer( TerrainLayerRole::lakeGravel ) : -1;
		params.lakeSiltLayer = params.waterBedLayer >= 0 ? m_pipeline->roleLayer( TerrainLayerRole::lakeSilt ) : -1;
		params.lakeBedDepths = XMFLOAT4( m_pipeline->layerRules().lakeGravelDepth.from, m_pipeline->layerRules().lakeGravelDepth.to,
										 m_pipeline->layerRules().lakeSiltDepth.from, m_pipeline->layerRules().lakeSiltDepth.to );
		params.lakeSedimentLevel = XMFLOAT2( m_pipeline->heightOffset() + m_pipeline->layerRules().lakeSedimentHeight.from * m_pipeline->heightMultiplier(),
											 m_pipeline->heightOffset() + m_pipeline->layerRules().lakeSedimentHeight.to * m_pipeline->heightMultiplier() );
		std::copy( std::begin( m_detailMorph ), std::end( m_detailMorph ), params.detailMorph );
	} );
	DMD3D::instance().setConstantBuffer( SLOT_CB_MATERIAL, m_constantBuffer );

	m_patchBuffer.updateData( patches.data(), sizeof( PatchInstance ) * patches.size() );
	m_patchBuffer.setToSlot( SLOT_INSTANCE_DATA );

	// Карта высот — вершинному шейдеру и пиксельному: по ней считается нормаль рельефа
	DMD3D::instance().setSRV( 0, m_pipeline->heightMapView() );
	// Нормали и шум слоёв — пиксельному шейдеру; splat-карта, альбедо с высотой и детальный слой весов — ресурсы сцены
	// (compute): их читает и дисплейсмент вершинного шейдера в проходах только глубины
	if( !depthOnly )
		m_pipeline->material().bind();
	if( !depthOnly && paintMask )
		DMD3D::instance().setSRV( 7, *paintMask );

	context.constants.setPerObjectBuffer( XMMatrixIdentity() );

	DMD3D::instance().setVertexBuffer( m_patch.vertexBuffer(), sizeof( XMFLOAT3 ) );
	DMD3D::instance().setIndexBuffer( m_patch.indexBuffer(), DXGI_FORMAT_R32_UINT );

	m_shader.setPass( depthOnly ? m_depthPhase : m_properties["Show LOD"].data<bool>() ? m_lodPhase : m_materialPhase );
	DMD3D::instance().drawIndexedInstanced( m_patch.indexCount(), static_cast<uint32_t>( patches.size() ), 0, 0 );
}

PropertyContainer* CDLODTerrain::properties()
{
	return &m_properties;
}

}
