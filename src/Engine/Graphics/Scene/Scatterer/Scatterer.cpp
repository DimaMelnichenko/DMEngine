#include "Scatterer.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
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

bool Scatterer::addLayer( const std::string& name, DMModel::LodBlock* lodBlock, const std::string& mask,
						  const ScatterPass::PopulateParams& params )
{
	Layer layer;
	layer.lodBlock = lodBlock;
	layer.material = System::materials().get( lodBlock->material )->m_shader.get();
	layer.mask = mask;
	layer.pass = std::make_unique<ScatterPass>();
	layer.pass->populateParams() = params;
	if( !layer.pass->createBuffers() )
		return false;

	layer.properties = std::make_unique<PropertyContainer>( std::to_string( m_layers.size() ) + ": " + name );
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

		const AbstractMesh* mesh = System::meshes().get( layer.lodBlock->mesh ).get();
		layer.pass->setInstanceParameters( m_initShader, mesh->indexCount(), mesh->indexOffset(), mesh->vertexOffset() );

		// Сетка покрывает квадрат со стороной 2 · farBorder вокруг камеры
		const float cells = std::ceil( 2.0f * params.farBorder / params.cellSize ) + 1.0f;
		const uint16_t gridDim = static_cast<uint16_t>( std::min( cells, static_cast<float>( maxGridDim ) ) );

		DMD3D::instance().setSRV( SRVType::cs, 2, System::textures().get( layer.mask )->srv() );
		layer.pass->populate( m_computeShader, gridDim );
	}
}

bool Scatterer::castsShadow( const Layer& layer ) const
{
	return ( *layer.properties )["Cast shadow"].data<bool>() && layer.material->depthPhaseFor( layer.lodBlock->params ) >= 0;
}

void Scatterer::collectMeshes( const RenderView&, MeshCollector& collector )
{
	// Режим читается каждый кадр: параметры материала меняются в GUI
	uint32_t passMask = 0;
	for( const Layer& layer : m_layers )
	{
		passMask |= passBit( passFor( layer.material->renderState( layer.lodBlock->params ).blendMode ) );
		if( castsShadow( layer ) )
			passMask |= passBit( MeshPass::csmShadowDepth );
	}
	if( passMask )
		collector.addCustom( passMask );
}

void Scatterer::renderCustom( const RenderContext& context )
{
	ScopedRenderState scatterState;

	XMMATRIX worldMatrix = XMMatrixIdentity();

	const bool shadow = context.pass == MeshPass::csmShadowDepth;
	for( Layer& layer : m_layers )
	{
		const MaterialRenderState state = layer.material->renderState( layer.lodBlock->params );
		if( shadow )
		{
			// Инстансы слоя — в кольце near…far вокруг камеры; тень от них ложится не дальше её длины. Каскад, диапазон
			// расстояний которого с кольцом не пересекается, этих теней не содержит
			const ScatterPass::PopulateParams& params = layer.pass->populateParams();
			const float margin = params.sizeMultiplier * m_shadowLength;
			if( !castsShadow( layer ) || context.view.cascadeNear >= params.farBorder + margin ||
				context.view.cascadeFar <= params.nearBorder - margin )
				continue;
		}
		else if( passFor( state.blendMode ) != context.pass )
		{
			continue;
		}
		DMD3D::instance().setState( materialRasterState( state.twoSided, false, context.frameRaster ) );

		DMShader* shader = layer.material;
		shader->setPass( shadow ? shader->depthPhaseFor( layer.lodBlock->params ) : shader->phaseFor( layer.lodBlock->params ) );
		shader->setParams( layer.lodBlock->params );
		shader->setDrawType( DMShader::by_index );

		// Инстансы слоя читают вершинные шейдеры с INST_POS, INST_SCALE и INST_ROTATE (Shaders\instance.sh)
		DMD3D::instance().setSRV( SRVType::vs, SLOT_INSTANCE_DATA, layer.pass->structuredBuffer() );

		context.constants.setPerObjectBuffer( worldMatrix );
		shader->renderInstancedIndirect( layer.pass->args() );
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
