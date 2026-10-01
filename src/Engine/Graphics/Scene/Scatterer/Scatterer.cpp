#include "Scatterer.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include "D3D\DMD3D.h"
#include "System.h"
#include "ConstantBuffers.h"
#include "Shaders\lod_transition.h"

namespace GS
{

Scatterer::Scatterer( const std::string& name ) :
	SceneObject( name )
{
	m_properties.setName( name );
	Property* shadowLod = m_properties.insert( "Shadow LOD scale", 1.0f );
	shadowLod->setLow( 0.1f );
	shadowLod->setHigh( 1.0f );
}

bool Scatterer::Initialize()
{
	if( !m_computeShader.Initialize( "Shaders\\scatter.cs", "main" ) )
		return false;

	if( !m_commandShader.Initialize( "Shaders\\scatter.cs", "buildCommands" ) )
		return false;

	return DMD3D::instance().createShaderConstantBuffer( sizeof( TerrainParams ), m_terrainBuffer ) &&
		   DMD3D::instance().createShaderConstantBuffer( sizeof( FrustumParams ), m_frustumBuffer );
}

void Scatterer::setTerrain( const TerrainHeightSource* terrain )
{
	m_terrain = terrain;
}

bool Scatterer::addLayer( const std::vector<LayerModel>& models, const std::string& mask, const ScatterLayerSettings& settings )
{
	if( models.empty() )
		return false;

	Layer layer;
	layer.mask = mask;
	layer.pass = std::make_unique<ScatterPass>();
	// Константы раскладки (Shaders\scatter.cs) из строки ScatterLayers; ёмкости и число вариантов — createBuffers()
	ScatterPass::PopulateParams& params = layer.pass->populateParams();
	params = {};
	params.cellSize = settings.cellSize;
	params.nearBorder = settings.nearBorder;
	params.farBorder = settings.farBorder;
	params.nearFade = settings.nearFade;
	params.farFade = settings.farFade;
	params.sizeMultiplier = settings.sizeMultiplier;
	params.jitter = settings.jitter;
	params.rotationRange = XMFLOAT3( XMConvertToRadians( settings.rotationRange.x ), XMConvertToRadians( settings.rotationRange.y ),
									 XMConvertToRadians( settings.rotationRange.z ) );
	params.alignToTerrain = settings.alignToTerrain ? 1.0f : 0.0f;
	params.castShadow = settings.castShadow ? 1.0f : 0.0f;

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
				section.material = System::materials().get( section.section->material ).get();
				lod.sections.push_back( section );
			}
			passVariant.sectionCount[i] = static_cast<uint32_t>( std::max<size_t>( sectionCount, 1 ) );
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

uint64_t Scatterer::hashParams( const PropertyContainer& params )
{
	// FNV-1a по именам и байтам значений в порядке добавления
	uint64_t hash = 14695981039346656037ull;
	const auto mix = [&hash]( const void* data, size_t size )
	{
		for( size_t i = 0; i < size; ++i )
		{
			hash ^= static_cast<const uint8_t*>( data )[i];
			hash *= 1099511628211ull;
		}
	};
	for( const std::string& name : params.names() )
	{
		mix( name.data(), name.size() );
		const Property& property = params.property( name );
		if( const bool* v = property.dataPtr<bool>() ) mix( v, sizeof( *v ) );
		else if( const float* f = property.dataPtr<float>() ) mix( f, sizeof( *f ) );
		else if( const XMFLOAT2* f2 = property.dataPtr<XMFLOAT2>() ) mix( f2, sizeof( *f2 ) );
		else if( const XMFLOAT3* f3 = property.dataPtr<XMFLOAT3>() ) mix( f3, sizeof( *f3 ) );
		else if( const XMFLOAT4* f4 = property.dataPtr<XMFLOAT4>() ) mix( f4, sizeof( *f4 ) );
		else if( const int32_t* i = property.dataPtr<int32_t>() ) mix( i, sizeof( *i ) );
		else if( const uint32_t* u = property.dataPtr<uint32_t>() ) mix( u, sizeof( *u ) );
	}
	return hash;
}

void Scatterer::assignGroups( Layer& layer )
{
	// Секции списков с одним материалом, параметрами и состоянием — одна группа: один ExecuteIndirect на вид. Порядок
	// обхода постоянен, поэтому у неизменившегося слоя таблицы не меняются и на GPU не уходят
	layer.groups.clear();
	const bool layerShadow = ( *layer.properties )["Cast shadow"].data<bool>();
	for( uint32_t v = 0; v < layer.variants.size(); ++v )
	{
		const LayerVariant& variant = layer.variants[v];
		for( uint32_t i = 0; i < variant.lods.size(); ++i )
		for( bool transition : { false, true } )
		{
			const uint32_t list = ScatterPass::listIndex( v, i, transition );
			const LayerLod& lod = variant.lods[i];
			for( uint32_t s = 0; s < ScatterPass::maxSections; ++s )
			{
				// Список перехода — экземпляры в полосе смены LOD; без дизеринга он пуст
				if( s >= lod.sections.size() || ( transition && !variant.ditheredLodTransition ) )
				{
					layer.pass->clearSectionArgs( list, s );
					continue;
				}
				const LayerSection& section = lod.sections[s];
				LayerGroup group;
				group.material = section.material;
				group.params = &section.section->params;
				group.paramsHash = hashParams( section.section->params );
				group.state = section.material->renderState( section.section->params );
				group.pass = passFor( group.state.blendMode );
				group.transition = transition;
				group.prepassed = inDepthPrepass( section );
				group.castShadow = layerShadow && castsShadow( layer, variant, section );

				uint32_t index = 0;
				for( ; index < layer.groups.size(); ++index )
				{
					const LayerGroup& other = layer.groups[index];
					if( other.material == group.material && other.paramsHash == group.paramsHash && other.transition == group.transition &&
						other.state.twoSided == group.state.twoSided && other.state.blendMode == group.state.blendMode &&
						other.prepassed == group.prepassed && other.castShadow == group.castShadow )
						break;
				}
				if( index == layer.groups.size() )
					layer.groups.push_back( group );

				const AbstractMesh* mesh = System::meshes().get( section.section->mesh ).get();
				layer.pass->setSectionArgs( list, s, mesh->indexCount(), mesh->indexOffset(), mesh->vertexOffset(), index );
			}
		}
	}
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
	DMD3D::instance().setConstantBuffer( 5, m_terrainBuffer );

	// Луч от верха инстанса до земли длиннее высоты в 1 / sin(высота солнца) раз; у горизонта — не больше 10
	const XMFLOAT3& toSun = frame.toShadowLight;
	m_shadowLength = toSun.y > 0.0f ? std::min( 1.0f / toSun.y, 10.0f ) : 0.0f;

	// Виды кадра: главный и каскады теней; у видов теней — множитель дальностей LOD, с ним < 1 переход дизерингом
	// выключен (доля перехода в пуле одна на все виды)
	const float shadowLodScale = m_properties["Shadow LOD scale"].data<float>();
	Device::updateResource<FrustumParams>( m_frustumBuffer, [&]( FrustumParams& params )
	{
		params = {};
		const uint32_t count = frame.viewCount ? std::min( frame.viewCount, maxRenderViews ) : 1;
		for( uint32_t v = 0; v < count; ++v )
		{
			const RenderView& view = frame.viewCount ? *frame.views[v] : frame.view;
			for( int i = 0; i < 6; ++i )
				XMStoreFloat4( &params.planes[v * 6 + i], view.frustum.planes()[i] );
			const float lodScale = v == 0 ? 1.0f : shadowLodScale;
			params.viewParams[v] = XMFLOAT4( lodScale, lodScale >= 0.999f ? 1.0f : 0.0f, view.cascadeNear, view.cascadeFar );
		}
		params.viewCount = count;
		params.shadowCast = XMFLOAT4( -toSun.x, -toSun.y, -toSun.z, m_shadowLength );
	} );
	DMD3D::instance().setConstantBuffer( 6, m_frustumBuffer );

	for( Layer& layer : m_layers )
	{
		const ScatterPass::PopulateParams& params = layer.pass->populateParams();
		if( params.cellSize <= 0.0f || params.farBorder <= 0.0f )
			continue;

		// Флаг тени из GUI: по нему compute раскладывает инстансы и в списки видов теней
		bool anyShadow = false;
		for( const LayerVariant& variant : layer.variants )
			anyShadow = anyShadow || variant.castShadow;
		layer.pass->populateParams().castShadow = anyShadow && ( *layer.properties )["Cast shadow"].data<bool>() ? 1.0f : 0.0f;

		// Смена LOD дизерингом — если её умеют материалы всех секций варианта
		for( uint32_t v = 0; v < layer.variants.size(); ++v )
		{
			LayerVariant& variant = layer.variants[v];
			variant.ditheredLodTransition = variant.lods.size() > 1;
			for( const LayerLod& lod : variant.lods )
			for( const LayerSection& section : lod.sections )
				variant.ditheredLodTransition = variant.ditheredLodTransition &&
												section.material->renderState( section.section->params ).ditheredLodTransition;
			layer.pass->setDitheredLodTransition( v, variant.ditheredLodTransition );
		}
		assignGroups( layer );

		// Сетка покрывает квадрат со стороной 2 · farBorder вокруг камеры
		const float cells = std::ceil( 2.0f * params.farBorder / params.cellSize ) + 1.0f;
		const uint16_t gridDim = static_cast<uint16_t>( std::min( cells, static_cast<float>( maxGridDim ) ) );

		// Проход раскладки слоя: читает карту высот и маску плотности, пишет счётчики (сначала нули), пулы, списки
		// индексов видов и команды
		const ShaderView& heightMap = System::textures().get( terrain.heightMap )->srv();
		const ShaderView& mask = System::textures().get( layer.mask )->srv();
		PassDesc pass = layer.pass->passDesc( "Scatter layer" );
		pass.reads = { { &heightMap, "height map" }, { &mask, "density mask" } };
		DMD3D::instance().beginPass( pass );
		layer.pass->resetCounters();
		DMD3D::instance().setSRV( 0, heightMap );
		DMD3D::instance().setSRV( 2, mask );
		layer.pass->populate( m_computeShader, gridDim );
		layer.pass->buildCommands( m_commandShader );
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
	DMD3D& d3d = DMD3D::instance();
	const XMMATRIX worldMatrix = XMMatrixIdentity();

	const bool shadow = context.pass == MeshPass::csmShadowDepth;
	const bool prepass = context.pass == MeshPass::depthPrepass;
	const uint32_t view = std::min( context.view.index, ScatterPass::maxViews - 1 );
	for( Layer& layer : m_layers )
	{
		const ScatterPass::PopulateParams& params = layer.pass->populateParams();
		// Инстансы слоя — не дальше кольца от камеры, их тень — не дальше её длины: каскад дальше этого их теней не содержит
		if( shadow && ( params.castShadow < 0.5f || context.view.cascadeNear >= params.farBorder + params.sizeMultiplier * m_shadowLength ) )
			continue;

		for( uint32_t g = 0; g < layer.groups.size(); ++g )
		{
			const LayerGroup& group = layer.groups[g];
			if( shadow ? !group.castShadow : prepass ? !group.prepassed : group.pass != context.pass )
				continue;

			// Группа, которой не было в depth prepass, в проходе цвета пишет глубину сама
			std::optional<ScopedRenderState> ownDepth;
			if( context.depthFromPrepass && !group.prepassed )
				ownDepth.emplace( DepthState::enabled );
			d3d.setState( materialRasterState( group.state.twoSided, false, context.frameRaster ) );

			// После depth prepass Masked и дизеринг не отсекают: маска уже в глубине, проверка EQUAL
			ShaderPhaseOptions options;
			options.depthFromPrepass = context.depthFromPrepass && group.prepassed;
			options.lodDither = group.transition;
			Material* shader = group.material;
			shader->setPass( isDepthOnlyPass( context.pass ) ? shader->depthPhaseFor( *group.params, options ) :
							 shader->phaseFor( *group.params, options ) );
			shader->setParams( *group.params );

			// Вершинный шейдер: индекс инстанса — из списка вида по root-константе команды (начало списка), сам инстанс —
			// из пула (INST_POS, INST_SCALE, INST_ROTATE — Shaders\instance.sh; у списка перехода — ещё LOD_DITHER)
			d3d.setSRV( SLOT_INSTANCE_INDICES, layer.pass->indices() );
			d3d.setSRV( SLOT_INSTANCE_DATA, group.transition ? layer.pass->transitions() : layer.pass->items() );
			context.constants.setPerObjectBuffer( worldMatrix );
			d3d.drawIndexedInstancedIndirectCount( layer.pass->commands(), layer.pass->commandsOffset( view, g ), layer.pass->groupCapacity( g ),
												   layer.pass->counters(), ScatterPass::groupCountOffset( view, g ) );
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
