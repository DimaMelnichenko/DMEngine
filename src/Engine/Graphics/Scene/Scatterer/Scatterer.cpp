#include "Scatterer.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include "D3D\DMD3D.h"
#include "System.h"
#include "Pipeline.h"
#include "Shaders\ConstantBuffers.h"

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

	if( !m_initShader.Initialize( "Shaders\\scatter.cs", "init" ) )
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
		for( uint32_t i = 0; i < passVariant.lodCount; ++i )
		{
			passVariant.lodEnd[i] = i + 1 < passVariant.lodCount ? model->lodRange( static_cast<uint16_t>( i ) ) : params.farBorder;
			LayerLod lod;
			lod.block = model->getLodById( static_cast<uint16_t>( i ) );
			lod.material = System::materials().get( lod.block->material )->m_shader.get();
			lod.nearDistance = std::max( params.nearBorder, i == 0 ? 0.0f : passVariant.lodEnd[i - 1] );
			lod.farDistance = std::min( params.farBorder, passVariant.lodEnd[i] );
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
	const XMFLOAT3& toSun = frame.toSun;
	m_shadowLength = toSun.y > 0.0f ? std::min( 1.0f / toSun.y, 10.0f ) : 0.0f;
	Device::updateResource<FrustumParams>( m_frustumBuffer, [&]( FrustumParams& params )
	{
		for( int i = 0; i < 6; ++i )
			XMStoreFloat4( &params.planes[i], frame.view.frustum.planes()[i] );
		params.shadowCast = XMFLOAT4( -toSun.x, -toSun.y, -toSun.z, m_shadowLength );
	} );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 6, m_frustumBuffer );

	DMD3D::instance().setSRV( SRVType::cs, 0, System::textures().get( terrain.heightMap )->srv() );

	for( Layer& layer : m_layers )
	{
		const ScatterPass::PopulateParams& params = layer.pass->populateParams();
		if( params.cellSize <= 0.0f || params.farBorder <= 0.0f )
			continue;

		// Флаг тени из GUI: по нему compute оставляет и инстансы за кадром, чья тень падает в кадр
		layer.pass->populateParams().castShadow = ( *layer.properties )["Cast shadow"].data<bool>() ? 1.0f : 0.0f;

		for( uint32_t v = 0; v < layer.variants.size(); ++v )
		{
			const std::vector<LayerLod>& lods = layer.variants[v].lods;
			for( uint32_t i = 0; i < lods.size(); ++i )
			{
				const AbstractMesh* mesh = System::meshes().get( lods[i].block->mesh ).get();
				layer.pass->resetArgs( m_initShader, ScatterPass::listIndex( v, i ), mesh->indexCount(), mesh->indexOffset(),
									   mesh->vertexOffset() );
			}
		}

		// Сетка покрывает квадрат со стороной 2 · farBorder вокруг камеры
		const float cells = std::ceil( 2.0f * params.farBorder / params.cellSize ) + 1.0f;
		const uint16_t gridDim = static_cast<uint16_t>( std::min( cells, static_cast<float>( maxGridDim ) ) );

		DMD3D::instance().setSRV( SRVType::cs, 2, System::textures().get( layer.mask )->srv() );
		layer.pass->populate( m_computeShader, gridDim );
	}
}

bool Scatterer::castsShadow( const Layer& layer, const LayerLod& lod ) const
{
	return ( *layer.properties )["Cast shadow"].data<bool>() && lod.material->depthPhaseFor( lod.block->params ) >= 0;
}

bool Scatterer::inDepthPrepass( const LayerLod& lod ) const
{
	return passFor( lod.material->renderState( lod.block->params ).blendMode ) == MeshPass::opaque &&
		   lod.material->depthPhaseFor( lod.block->params ) >= 0;
}

void Scatterer::collectMeshes( const RenderView&, MeshCollector& collector )
{
	// Режим читается каждый кадр: параметры материала меняются в GUI
	uint32_t passMask = 0;
	for( const Layer& layer : m_layers )
	{
		for( const LayerVariant& variant : layer.variants )
		for( const LayerLod& lod : variant.lods )
		{
			passMask |= passBit( passFor( lod.material->renderState( lod.block->params ).blendMode ) );
			if( inDepthPrepass( lod ) )
				passMask |= passBit( MeshPass::depthPrepass );
			if( castsShadow( layer, lod ) )
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
		{
			const LayerLod& lod = layer.variants[v].lods[i];
			const uint32_t list = ScatterPass::listIndex( v, i );
			if( lod.nearDistance >= lod.farDistance )
				continue;	// LOD не попадает в кольцо слоя

			const MaterialRenderState state = lod.material->renderState( lod.block->params );
			const bool prepassed = inDepthPrepass( lod );
			if( shadow )
			{
				// Инстансы LOD — на расстояниях near…far от камеры; тень от них ложится не дальше её длины. Каскад,
				// диапазон расстояний которого с этим не пересекается, их теней не содержит
				if( !castsShadow( layer, lod ) || context.view.cascadeNear >= lod.farDistance + margin ||
					context.view.cascadeFar <= lod.nearDistance - margin )
					continue;
			}
			else if( prepass ? !prepassed : passFor( state.blendMode ) != context.pass )
			{
				continue;
			}
			// Слой, которого не было в depth prepass, в проходе цвета пишет глубину сам
			std::optional<ScopedRenderState> ownDepth;
			if( context.depthFromPrepass && !prepassed )
				ownDepth.emplace( DepthState::enabled );
			DMD3D::instance().setState( materialRasterState( state.twoSided, false, context.frameRaster ) );

			DMShader* shader = lod.material;
			// После depth prepass Masked не отсекает по альфе: маска уже в глубине, проверка EQUAL
			shader->setPass( isDepthOnlyPass( context.pass ) ? shader->depthPhaseFor( lod.block->params ) :
							 shader->phaseFor( lod.block->params, false, context.depthFromPrepass && prepassed ) );
			shader->setParams( lod.block->params );
			shader->setDrawType( DMShader::by_index );

			// Инстансы LOD читают вершинные шейдеры с INST_POS, INST_SCALE и INST_ROTATE (Shaders\instance.sh)
			DMD3D::instance().setSRV( SRVType::vs, SLOT_INSTANCE_DATA, layer.pass->instances( list ) );

			context.constants.setPerObjectBuffer( worldMatrix );
			shader->renderInstancedIndirect( layer.pass->args(), ScatterPass::argsOffset( list ) );
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
