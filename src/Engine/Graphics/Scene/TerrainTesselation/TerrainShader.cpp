#include "TerrainShader.h"
#include "D3D/DMD3D.h"
#include "System.h"

namespace GS
{

TerrainShader::TerrainShader()
{

}

TerrainShader::~TerrainShader()
{

}

bool TerrainShader::initialize()
{
	m_properties.setName( "Shader parameters:" );
	auto prop = m_properties.insert( "Outer tess", 1.0f );
	prop->setLow( 1.0 );
	prop->setHigh( 16.0 );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Inner tess", 1.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "High multipler", 10.0f );
	prop->setLow( 1.0 );
	prop->setHigh( 1000.0 );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Wireframe", false );


	std::vector<D3D11_INPUT_ELEMENT_DESC> vertexLayout;
	D3D11_INPUT_ELEMENT_DESC polygonLayout;
	polygonLayout.SemanticName = "POSITION";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = 0;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;
	vertexLayout.push_back( polygonLayout );

	m_shader.setLayoutDesc( std::move( vertexLayout ) );

	std::string fileName( "Shaders\\TessTerrain.fx" );

	if( !m_shader.addShaderPassFromFile( SRVType::vs, "VS_RenderSceneWithTessellation", fileName ) )
	{
		LOG( "Не получилось скомпилить шейдер" );
		return false;
	}

	if( !m_shader.addShaderPassFromFile( SRVType::hs, "HS_Quads_fractionaleven", fileName ) )
	{
		LOG( "Не получилось скомпилить шейдер" );
		return false;
	}

	if( !m_shader.addShaderPassFromFile( SRVType::ds, "DS_Quads", fileName ) )
	{
		LOG( "Не получилось скомпилить шейдер" );
		return false;
	}

	if( !m_shader.addShaderPassFromFile( SRVType::ps, "PS_SolidColor", fileName ) )
	{
		LOG( "Не получилось скомпилить шейдер" );
		return false;
	}

	if( !m_shader.createPhase( 0, 0, -1, 0, 0 ) )
		return false;




	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constBuffer ) )
		return false;

	return true;
}

void TerrainShader::render( XMFLOAT2& snapOffset, DMStructuredBuffer& structBuffer, uint32_t indexCount, uint32_t instanceCount )
{
	Device::updateResource<TerrainShader::Parameters>( m_constBuffer, [this, &snapOffset]( TerrainShader::Parameters& param )
	{
		param.tessFactors.x = param.tessFactors.y = param.tessFactors.z = param.tessFactors.w = m_properties["Outer tess"].data<float>();
		param.innerTess = m_properties["Inner tess"].data<float>();
		param.hightMultipler = m_properties["High multipler"].data<float>();
		param.snapOffset = snapOffset;
	} );

	DMD3D::instance().setSRV( SRVType::vs, 0, System::textures().get( "t_heightmap" )->srv() );

	DMD3D::instance().setSRV( SRVType::ds, 0, System::textures().get( "t_heightmap" )->srv() );
	DMD3D::instance().setSRV( SRVType::ds, 3, System::textures().get( "knot_height" )->srv() );
	//DMD3D::instance().setSRV( SRVType::ds, 4, System::textures().get( "t_boulders_normal" )->srv() );
	DMD3D::instance().setSRV( SRVType::ds, 4, System::textures().get( "t_rock_normal" )->srv() );

	DMD3D::instance().setSRV( SRVType::ps, 1, System::textures().get( "t_normalmap" )->srv() );
	//DMD3D::instance().setSRV( SRVType::ps, 2, System::textures().get( "grass.jpg" )->srv() );
	DMD3D::instance().setSRV( SRVType::ps, 2, System::textures().get( "t_rock" )->srv() );
	DMD3D::instance().setSRV( SRVType::ps, 4, System::textures().get( "t_rock_normal" )->srv() );


	DMD3D::instance().setConstantBuffer( SRVType::vs, 2, m_constBuffer );
	DMD3D::instance().setConstantBuffer( SRVType::ds, 2, m_constBuffer );
	DMD3D::instance().setConstantBuffer( SRVType::hs, 2, m_constBuffer );

	structBuffer.setToSlot( 16, SRVType::vs );

	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST );
	m_shader.setDrawType( GS::DMShader::by_index_instance );
	m_shader.setPass( 0 );
	m_shader.renderInstanced( indexCount, 0, 0, instanceCount );
}

PropertyContainer* TerrainShader::properties()
{
	return &m_properties;
}

}