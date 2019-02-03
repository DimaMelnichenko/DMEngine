#include "Terrain.h"
#include "D3D/DMD3D.h"
#include "Logger/Logger.h"
#include <cmath>

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
	if( !m_shader.initialize() )
		return false;

	if( !m_path.reinitializeBuffers( m_pathSize ) )
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

	m_properties.setName( "Terrain settings:" );

	m_properties.addSubContainer( m_shader.properties() );

	return true;
}

XMFLOAT2& Terrain::calcWorldOffset( const XMFLOAT3& cameraPosition )
{
	static XMFLOAT2 snapOffset;
	snapOffset.x = cameraPosition.x;
	snapOffset.y = cameraPosition.z;

	static float temp  = m_pathSize / 1.0f;
	static float temp2 = m_pathSize / 1.0f;

	snapOffset.x = floorf( snapOffset.x * ( 1.0f / temp ) ) * temp2;
	snapOffset.y = floorf( snapOffset.y * ( 1.0f / temp ) ) * temp2;

	const float dx = cameraPosition.x - snapOffset.x;
	const float dz = cameraPosition.z - snapOffset.y;
	snapOffset.x = cameraPosition.x - dx;
	snapOffset.y = cameraPosition.z - dz;
	return snapOffset;
}

void Terrain::render( const XMFLOAT3& cameraPosition )
{
	XMFLOAT2& snapOffset = calcWorldOffset( cameraPosition );

	UINT Stride = sizeof( XMFLOAT3 );
	UINT Offset = 0;

	ID3D11Buffer* vertexBuffer = m_path.vertexBuffer();
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &vertexBuffer, &Stride, &Offset );
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_path.indexBuffer(), DXGI_FORMAT_R32_UINT, 0 );

	m_shader.render( snapOffset, m_structuredBuffer, m_path.indexCount(), m_structuredBuffer.numElements() );
}

PropertyContainer* Terrain::properties()
{
	return &m_properties;
}

bool Terrain::wireframe()
{
	bool* value = m_properties["Wireframe"].dataPtr<bool>();
	if( value )
		return *value;
	else
		return false;
}

}