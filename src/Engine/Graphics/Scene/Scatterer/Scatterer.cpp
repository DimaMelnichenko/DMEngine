#include "Scatterer.h"
#include <algorithm>
#include <cmath>
#include "D3D\DMD3D.h"
#include "System.h"
#include "Pipeline.h"

namespace GS
{

Scatterer::Scatterer( const std::string& name, RenderPass pass, bool twoSided ) :
	SceneObject( name, pass ),
	m_twoSided( twoSided )
{
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

void Scatterer::setColorTexture( const std::string& texture )
{
	m_colorTexture = texture;
}

bool Scatterer::addLayer( DMModel::LodBlock* lodBlock, const std::string& mask, const ScatterPass::PopulateParams& params )
{
	Layer layer;
	layer.lodBlock = lodBlock;
	layer.mask = mask;
	layer.pass = std::make_unique<ScatterPass>();
	layer.pass->populateParams() = params;
	if( !layer.pass->createBuffers() )
		return false;

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
		params.heightMultipler = terrain.heightMultipler;
		params.heightOffset = terrain.heightOffset;
	} );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 5, m_terrainBuffer );

	Device::updateResource<FrustumParams>( m_frustumBuffer, [&frame]( FrustumParams& params )
	{
		for( int i = 0; i < 6; ++i )
			XMStoreFloat4( &params.planes[i], frame.frustum.planes()[i] );
	} );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 6, m_frustumBuffer );

	DMD3D::instance().setSRV( SRVType::cs, 0, System::textures().get( terrain.heightMap )->srv() );

	for( Layer& layer : m_layers )
	{
		const ScatterPass::PopulateParams& params = layer.pass->populateParams();
		if( params.cellSize <= 0.0f || params.farBorder <= 0.0f )
			continue;

		const AbstractMesh* mesh = System::meshes().get( layer.lodBlock->mesh ).get();
		layer.pass->setInstanceParameters( m_initShader, mesh->indexCount(), mesh->indexOffset(), mesh->vertexOffset() );

		// Сетка покрывает квадрат со стороной 2 · farBorder вокруг камеры
		const float cells = std::ceil( 2.0f * params.farBorder / params.cellSize ) + 1.0f;
		const uint16_t gridDim = static_cast<uint16_t>( std::min( cells, static_cast<float>( maxGridDim ) ) );

		DMD3D::instance().setSRV( SRVType::cs, 2, System::textures().get( layer.mask )->srv() );
		layer.pass->populate( m_computeShader, gridDim );
	}
}

void Scatterer::render( const FrameContext& frame )
{
	if( m_layers.empty() )
		return;

	ScopedRenderState scatterState;
	if( m_twoSided )
		DMD3D::instance().setState( RasterState::noCulling );

	if( !m_colorTexture.empty() )
		DMD3D::instance().setSRV( SRVType::ps, 1, System::textures().get( m_colorTexture )->srv() );

	XMMATRIX worldMatrix = XMMatrixIdentity();

	for( Layer& layer : m_layers )
	{
		DMShader* shader = System::materials().get( layer.lodBlock->material )->m_shader.get();
		shader->setParams( layer.lodBlock->params );
		shader->setPass( 0 );
		shader->setDrawType( DMShader::by_index );

		// Инстансы слоя читают вершинные шейдеры с INST_POS, INST_SCALE и INST_ROTATE (Shaders\instance.sh)
		DMD3D::instance().setSRV( SRVType::vs, 16, layer.pass->structuredBuffer() );

		pipeline().shaderConstant().setPerObjectBuffer( &worldMatrix );
		shader->renderInstancedIndirect( layer.pass->args() );
	}
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
