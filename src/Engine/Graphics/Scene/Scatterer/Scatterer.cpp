#include "Scatterer.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include "D3D\DMD3D.h"
#include "System.h"
#include "Pipeline.h"
#include "Shaders\ConstantBuffers.h"
#include "Shaders\lod_transition.h"

namespace GS
{

Scatterer::Scatterer( const std::string& name ) :
	SceneObject( name )
{
	m_properties.setName( name );
}

bool Scatterer::Initialize()
{
	if( !m_computeShader.Initialize( "Shaders\\scatter.cs", "main" ) )
		return false;

	if( !m_sectionCountShader.Initialize( "Shaders\\scatter.cs", "copySectionCounts" ) )
		return false;

	return DMD3D::instance().createShaderConstantBuffer( sizeof( TerrainParams ), m_terrainBuffer ) &&
		   DMD3D::instance().createShaderConstantBuffer( sizeof( FrustumParams ), m_frustumBuffer );
}

void Scatterer::setTerrain( const TerrainHeightSource* terrain )
{
	m_terrain = terrain;
}

bool Scatterer::addLayer( const std::vector<LayerModel>& models, const std::string& mask, const ScatterPass::PopulateParams& params )
{
	if( models.empty() )
		return false;

	Layer layer;
	layer.mask = mask;
	layer.pass = std::make_unique<ScatterPass>();
	layer.pass->populateParams() = params;

	// Дальности LOD — из модели, как у моделей уровня; последний LOD рисуется до конца кольца
	std::vector<ScatterPass::Variant> passVariants;
	const size_t variantCount = std::min<size_t>( models.size(), ScatterPass::maxVariants );
	for( size_t v = 0; v < variantCount; ++v )
	{
		DMModel* model = models[v].model;
		ScatterPass::Variant passVariant;
		passVariant.weight = models[v].weight;
		passVariant.lodCount = std::min<uint32_t>( model->lodCount(), ScatterPass::maxLods );
		LayerVariant variant;
		variant.castShadow = models[v].castShadow;
		for( uint32_t i = 0; i < passVariant.lodCount; ++i )
		{
			passVariant.lodEnd[i] = i + 1 < passVariant.lodCount ? model->lodRange( static_cast<uint16_t>( i ) ) : params.farBorder;
			LayerLod lod;
			DMModel::LodBlock* block = model->getLodById( static_cast<uint16_t>( i ) );
			const size_t sectionCount = std::min<size_t>( block->sections.size(), ScatterPass::maxSections );
			for( size_t s = 0; s < sectionCount; ++s )
			{
				LayerSection section;
				section.section = block->sections[s].get();
				section.material = System::materials().get( section.section->material )->m_shader.get();
				lod.sections.push_back( section );
			}
			passVariant.sectionCount[i] = static_cast<uint32_t>( std::max<size_t>( sectionCount, 1 ) );
			// Дальности экземпляров ближе дальностей модели на разброс (Shaders\lod_transition.h); полосы перехода — вокруг них
			const bool hasNext = i + 1 < passVariant.lodCount;
			lod.nearDistance = std::max( params.nearBorder, i == 0 ? 0.0f : passVariant.lodEnd[i - 1] * LOD_JITTER_MIN_SCALE );
			lod.farDistance = std::min( params.farBorder, hasNext ? passVariant.lodEnd[i] : params.farBorder );
			if( passVariant.lodCount > 1 )
			{
				lod.transitionNear = std::max( params.nearBorder, passVariant.lodEnd[i == 0 ? 0 : i - 1] * LOD_DISTANCE_MIN_SCALE );
				lod.transitionFar = std::min( params.farBorder, passVariant.lodEnd[hasNext ? i : i - 1] * LOD_DISTANCE_MAX_SCALE );
			}
			variant.lods.push_back( lod );
		}
		layer.variants.push_back( std::move( variant ) );
		passVariants.push_back( passVariant );
	}
	if( !layer.pass->createBuffers( passVariants ) )
		return false;

	// Имя окна слоя — первая модель и число остальных
	std::string name = std::to_string( m_layers.size() ) + ": " + models[0].model->properties()->name();
	if( variantCount > 1 )
		name += " + " + std::to_string( variantCount - 1 );
	layer.properties = std::make_unique<PropertyContainer>( name );
	layer.properties->insert( "Cast shadow", params.castShadow > 0.5f );
	m_properties.addSubContainer( layer.properties.get() );

	m_layers.push_back( std::move( layer ) );
	return true;
}

void Scatterer::compute( const FrameContext& frame )
{
	if( !m_computeEnabled || !m_terrain || m_layers.empty() )
		return;

	const TerrainHeight terrain = m_terrain->terrainHeight();
	Device::updateResource<TerrainParams>( m_terrainBuffer, [&terrain]( TerrainParams& params )
	{
		params.worldSize = terrain.worldSize;
		params.heightMultiplier = terrain.heightMultiplier;
		params.heightOffset = terrain.heightOffset;
	} );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 5, m_terrainBuffer );

	// Луч от верха инстанса до земли длиннее высоты в 1 / sin(высота солнца) раз; у горизонта — не больше 10
	const XMFLOAT3& toSun = frame.toShadowLight;
	m_shadowLength = toSun.y > 0.0f ? std::min( 1.0f / toSun.y, 10.0f ) : 0.0f;
	Device::updateResource<FrustumParams>( m_frustumBuffer, [&]( FrustumParams& params )
	{
		for( int i = 0; i < 6; ++i )
			XMStoreFloat4( &params.planes[i], frame.view.frustum.planes()[i] );
		params.shadowCast = XMFLOAT4( -toSun.x, -toSun.y, -toSun.z, m_shadowLength );
	} );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 6, m_frustumBuffer );

	for( Layer& layer : m_layers )
	{
		const ScatterPass::PopulateParams& params = layer.pass->populateParams();
		if( params.cellSize <= 0.0f || params.farBorder <= 0.0f )
			continue;

		// Флаг тени из GUI: по нему compute оставляет и инстансы за кадром, чья тень падает в кадр
		bool anyShadow = false;
		for( const LayerVariant& variant : layer.variants )
			anyShadow = anyShadow || variant.castShadow;
		layer.pass->populateParams().castShadow = anyShadow && ( *layer.properties )["Cast shadow"].data<bool>() ? 1.0f : 0.0f;

		// Смена LOD дизерингом — если её умеют материалы всех секций варианта; начальные аргументы — меш каждой секции
		// (у обычного списка и списка перехода) и ноль инстансов, в буфер — одной записью
		for( uint32_t v = 0; v < layer.variants.size(); ++v )
		{
			LayerVariant& variant = layer.variants[v];
			variant.ditheredLodTransition = variant.lods.size() > 1;
			for( const LayerLod& lod : variant.lods )
			for( const LayerSection& section : lod.sections )
				variant.ditheredLodTransition = variant.ditheredLodTransition &&
												section.material->renderState( section.section->params ).ditheredLodTransition;
			layer.pass->setDitheredLodTransition( v, variant.ditheredLodTransition );

			for( uint32_t i = 0; i < variant.lods.size(); ++i )
			for( uint32_t s = 0; s < variant.lods[i].sections.size(); ++s )
			{
				const AbstractMesh* mesh = System::meshes().get( variant.lods[i].sections[s].section->mesh ).get();
				for( bool transition : { false, true } )
					layer.pass->setSectionArgs( ScatterPass::listIndex( v, i, transition ), s, mesh->indexCount(),
												mesh->indexOffset(), mesh->vertexOffset() );
			}
		}

		// Сетка покрывает квадрат со стороной 2 · farBorder вокруг камеры
		const float cells = std::ceil( 2.0f * params.farBorder / params.cellSize ) + 1.0f;
		const uint16_t gridDim = static_cast<uint16_t>( std::min( cells, static_cast<float>( maxGridDim ) ) );

		// Проход раскладки слоя: читает карту высот и маску плотности, пишет аргументы (сначала копия начальных) и инстансы
		const ShaderView& heightMap = System::textures().get( terrain.heightMap )->srv();
		const ShaderView& mask = System::textures().get( layer.mask )->srv();
		PassDesc pass = layer.pass->passDesc( "Scatter layer" );
		pass.reads = { { &heightMap, "height map" }, { &mask, "density mask" } };
		DMD3D::instance().beginPass( pass );
		layer.pass->resetArgs();
		DMD3D::instance().setSRV( SRVType::cs, 0, heightMap );
		DMD3D::instance().setSRV( SRVType::cs, 2, mask );
		layer.pass->populate( m_computeShader, gridDim );
		layer.pass->copySectionCounts( m_sectionCountShader );
	}
}

bool Scatterer::castsShadow( const Layer& layer, const LayerVariant& variant, const LayerSection& section ) const
{
	return variant.castShadow && ( *layer.properties )["Cast shadow"].data<bool>() &&
		   section.material->depthPhaseFor( section.section->params ) >= 0;
}

bool Scatterer::inDepthPrepass( const LayerSection& section ) const
{
	return passFor( section.material->renderState( section.section->params ).blendMode ) == MeshPass::opaque &&
		   section.material->depthPhaseFor( section.section->params ) >= 0;
}

void Scatterer::collectMeshes( const RenderView&, MeshCollector& collector )
{
	// Режим читается каждый кадр: параметры материала меняются в GUI
	uint32_t passMask = 0;
	for( const Layer& layer : m_layers )
	{
		for( const LayerVariant& variant : layer.variants )
		for( const LayerLod& lod : variant.lods )
		for( const LayerSection& section : lod.sections )
		{
			passMask |= passBit( passFor( section.material->renderState( section.section->params ).blendMode ) );
			if( inDepthPrepass( section ) )
				passMask |= passBit( MeshPass::depthPrepass );
			if( castsShadow( layer, variant, section ) )
				passMask |= passBit( MeshPass::csmShadowDepth );
		}
	}
	if( passMask )
		collector.addCustom( passMask );
}

void Scatterer::renderCustom( const RenderContext& context )
{
	ScopedRenderState scatterState;

	XMMATRIX worldMatrix = XMMatrixIdentity();

	const bool shadow = context.pass == MeshPass::csmShadowDepth;
	const bool prepass = context.pass == MeshPass::depthPrepass;
	for( Layer& layer : m_layers )
	{
		const float margin = layer.pass->populateParams().sizeMultiplier * m_shadowLength;
		for( uint32_t v = 0; v < layer.variants.size(); ++v )
		for( uint32_t i = 0; i < layer.variants[v].lods.size(); ++i )
		for( bool transition : { false, true } )
		{
			const LayerVariant& variant = layer.variants[v];
			const LayerLod& lod = variant.lods[i];
			const uint32_t list = ScatterPass::listIndex( v, i, transition );
			// Список перехода — экземпляры в полосе смены LOD, у каждого LOD своя доля пикселей; без дизеринга он пуст
			if( transition && !variant.ditheredLodTransition )
				continue;
			const float nearDistance = transition ? lod.transitionNear : lod.nearDistance;
			const float farDistance = transition ? lod.transitionFar : lod.farDistance;
			if( nearDistance >= farDistance )
				continue;	// список не попадает в кольцо слоя

			// Секция — свой вызов по тому же списку инстансов, проход — по режиму её материала
			for( uint32_t s = 0; s < lod.sections.size(); ++s )
			{
				const LayerSection& section = lod.sections[s];
				const PropertyContainer& params = section.section->params;
				const MaterialRenderState state = section.material->renderState( params );
				const bool prepassed = inDepthPrepass( section );
				if( shadow )
				{
					// Инстансы LOD — на расстояниях near…far от камеры; тень от них ложится не дальше её длины. Каскад,
					// диапазон расстояний которого с этим не пересекается, их теней не содержит
					if( !castsShadow( layer, variant, section ) || context.view.cascadeNear >= farDistance + margin ||
						context.view.cascadeFar <= nearDistance - margin )
						continue;
				}
				else if( prepass ? !prepassed : passFor( state.blendMode ) != context.pass )
				{
					continue;
				}
				// Секция, которой не было в depth prepass, в проходе цвета пишет глубину сама
				std::optional<ScopedRenderState> ownDepth;
				if( context.depthFromPrepass && !prepassed )
					ownDepth.emplace( DepthState::enabled );
				DMD3D::instance().setState( materialRasterState( state.twoSided, false, context.frameRaster ) );

				DMShader* shader = section.material;
				// После depth prepass Masked и дизеринг не отсекают: маска уже в глубине, проверка EQUAL
				ShaderPhaseOptions options;
				options.depthFromPrepass = context.depthFromPrepass && prepassed;
				options.lodDither = transition;
				shader->setPass( isDepthOnlyPass( context.pass ) ? shader->depthPhaseFor( params, options ) :
								 shader->phaseFor( params, options ) );
				shader->setParams( params );
				shader->setDrawType( DMShader::by_index );

				// Инстансы LOD читают вершинные шейдеры с INST_POS, INST_SCALE и INST_ROTATE (Shaders\instance.sh), списка
				// перехода — ещё и с LOD_DITHER
				DMD3D::instance().setSRV( SRVType::vs, SLOT_INSTANCE_DATA, layer.pass->instances( list ) );

				context.constants.setPerObjectBuffer( worldMatrix );
				shader->renderInstancedIndirect( layer.pass->args(), ScatterPass::argsOffset( list, s ) );
			}
		}
	}
}

PropertyContainer* Scatterer::properties()
{
	return &m_properties;
}

void Scatterer::setComputeEnabled( bool enabled )
{
	m_computeEnabled = enabled;
}

bool Scatterer::computeEnabled() const
{
	return m_computeEnabled;
}

}
