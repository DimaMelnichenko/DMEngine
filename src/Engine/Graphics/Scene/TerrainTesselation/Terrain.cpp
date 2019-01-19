#include "Terrain.h"
#include "D3D/DMD3D.h"
#include "Logger/Logger.h"
#include <cmath>
#include "System.h"
#include "Properties/Property.h"

namespace GS
{

Terrain::Terrain() :
	m_ringCount(7),
	m_pathSize(64)
{

}

Terrain::~Terrain()
{

}


bool Terrain::initialize()
{
	BasicProperty* prop = m_properties.moveProperty( new Property( "Outer tess", 1.0f ) );
	prop->setLow( 1.0 );
	prop->setHigh( 16.0 );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.moveProperty( new Property( "Inner tess", 1.0f ) );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.moveProperty( new Property( "High multipler", 10.0f ) );
	prop->setLow( 1.0 );
	prop->setHigh( 1000.0 );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.moveProperty( new Property( "Wireframe", false ) );


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


	if( !m_path.reinitializeBuffers( m_pathSize ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Param ), m_constBuffer ) )
		return false;

	struct PathParameters
	{
		XMFLOAT2 offset;
		float sizeMultipler;
		float dump;
	};

	m_structuredBuffer.createBuffer( sizeof( PathParameters ), 4 + m_ringCount * 12 );

	float worldMultipler = 1.0;
	
	std::vector<PathParameters> pathPosition;
	//central path
	float offset = (float)m_pathSize / 2.0f * worldMultipler;
	pathPosition.push_back( { XMFLOAT2( -offset, -offset ), 1.0f * worldMultipler } );
	pathPosition.push_back( { XMFLOAT2(  offset, -offset ), 1.0f * worldMultipler } );
	pathPosition.push_back( { XMFLOAT2(  offset,  offset ), 1.0f * worldMultipler } );
	pathPosition.push_back( { XMFLOAT2( -offset,  offset ), 1.0f * worldMultipler } );
															   
	//create rings
	for( uint16_t ringIndex = 0; ringIndex < m_ringCount; ++ringIndex )
	{
		float sizeMultipler = pow( 2, (float)ringIndex ) * worldMultipler;
		float offset = pow( 2, (float)ringIndex ) * m_pathSize * 0.5 * worldMultipler;
		float diagonalOffset = offset * 3.0;
		
		float offsetIncrementer = 1;
		pathPosition.push_back( { XMFLOAT2( -diagonalOffset,   diagonalOffset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2( -diagonalOffset,           offset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2( -diagonalOffset,		  -offset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2( -diagonalOffset,  -diagonalOffset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2(		    -offset,  -diagonalOffset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2(		     offset,  -diagonalOffset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2(  diagonalOffset,  -diagonalOffset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2(  diagonalOffset,          -offset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2(  diagonalOffset,           offset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2(  diagonalOffset,   diagonalOffset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2(  offset,           diagonalOffset ), sizeMultipler } );
		pathPosition.push_back( { XMFLOAT2( -offset,           diagonalOffset ), sizeMultipler } );
	}

	m_structuredBuffer.updateData( &pathPosition[0], m_structuredBuffer.sizeofElement() * m_structuredBuffer.numElements() );

	return true;
}

XMMATRIX Terrain::worldMatrix( const XMFLOAT3& cameraPosition )
{
	float snappedX = cameraPosition.x;
	float snappedZ = cameraPosition.z;

	static float temp  = m_pathSize / 2.0;
	static float temp2 = m_pathSize / 2.0;

	snappedZ = floorf( snappedZ * ( 1.0 / temp ) ) * temp2;
	snappedX = floorf( snappedX * ( 1.0 / temp ) ) * temp2;

	const float dx = cameraPosition.x - snappedX;
	const float dz = cameraPosition.z - snappedZ;
	snappedX = cameraPosition.x - dx;		
	snappedZ = cameraPosition.z - dz;
	return XMMatrixTranslation( snappedX, 0, snappedZ );
}

void Terrain::render()
{
	Device::updateResource<Terrain::Param>( m_constBuffer.get(), [this]( Terrain::Param& param )
	{
		param.tessFactors.x = param.tessFactors.y = param.tessFactors.z = param.tessFactors.w = *readProperty<float>( m_properties["Outer tess"] );
		param.modes.x = 0.0;
		param.modes.y = *readProperty<float>( m_properties["Inner tess"] );
		param.hightMultipler.x = *readProperty<float>( m_properties["High multipler"] );
	} );
	

	DMD3D::instance().setSRV( SRVType::ds, 0, System::textures().get( "t_heightmap" )->srv() );
	DMD3D::instance().setSRV( SRVType::ps, 1, System::textures().get( "t_normalmap" )->srv() );
	DMD3D::instance().setSRV( SRVType::ps, 2, System::textures().get( "grass.jpg" )->srv() );


	DMD3D::instance().setConstantBuffer( SRVType::ds, 2, m_constBuffer );
	DMD3D::instance().setConstantBuffer( SRVType::hs, 2, m_constBuffer );

	UINT Stride = sizeof( XMFLOAT3 );
	UINT Offset = 0;

	ID3D11Buffer* vertexBuffer = m_path.vertexBuffer();
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &vertexBuffer, &Stride, &Offset );
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_path.indexBuffer(), DXGI_FORMAT_R32_UINT, 0 );

	m_structuredBuffer.setToSlot( 16, SRVType::vs );
	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST );
	m_shader.setDrawType( GS::DMShader::by_index_instance );
	m_shader.setPass( 0 );
	m_shader.renderInstanced( m_path.indexCount(), 0, 0, m_structuredBuffer.numElements() );
}

PropertyContainer* Terrain::properties()
{
	return &m_properties;
}

bool Terrain::wireframe()
{
	return *readProperty<bool>( m_properties["Wireframe"] );
}

}