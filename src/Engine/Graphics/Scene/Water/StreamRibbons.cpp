#include "StreamRibbons.h"
#include <cmath>
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace GS
{

bool StreamRibbons::initialize( const std::vector<WaterStream>& streams )
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	for( const WaterStream& stream : streams )
	{
		const std::vector<WaterStreamPoint>& points = stream.points;
		if( points.size() < 2 )
			continue;
		const uint32_t first = static_cast<uint32_t>( vertices.size() );
		for( size_t i = 0; i < points.size(); ++i )
		{
			// Касательная — по соседним точкам, поперёк — перпендикуляр в плоскости XZ
			const XMFLOAT3& before = points[i > 0 ? i - 1 : i].position;
			const XMFLOAT3& after = points[i + 1 < points.size() ? i + 1 : i].position;
			float tx = after.x - before.x;
			float tz = after.z - before.z;
			const float length = std::max( std::sqrt( tx * tx + tz * tz ), 1e-6f );
			tx /= length;
			tz /= length;
			const WaterStreamPoint& point = points[i];
			const XMFLOAT4 flow( tx * point.speed, tz * point.speed, point.foam, 0.0f );
			for( float side : { -1.0f, 1.0f } )
			{
				Vertex vertex;
				vertex.position = XMFLOAT3( point.position.x - tz * point.halfWidth * side, point.position.y,
											point.position.z + tx * point.halfWidth * side );
				vertex.flow = flow;
				vertex.flow.w = side;
				vertices.push_back( vertex );
			}
			if( i > 0 )
			{
				const uint32_t a = first + static_cast<uint32_t>( i - 1 ) * 2;
				indices.insert( indices.end(), { a, a + 2, a + 1, a + 1, a + 2, a + 3 } );
			}
		}
	}
	if( indices.empty() )
		return true;

	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createVertexBuffer( m_vertexBuffer, vertices.data(), static_cast<uint32_t>( vertices.size() * sizeof( Vertex ) ) ) ||
		!d3d.createIndexBuffer( m_indexBuffer, indices.data(), static_cast<uint32_t>( indices.size() * sizeof( uint32_t ) ) ) )
	{
		LOG( "Stream ribbons: buffers are not created" );
		return false;
	}
	d3d.setName( m_vertexBuffer, "Stream ribbons vertices" );
	d3d.setName( m_indexBuffer, "Stream ribbons indices" );

	m_program.setLayoutDesc( { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 },
							   { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12 } } );
	if( !m_program.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\stream_water.vs" ) ||
		!m_program.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\stream_water.ps" ) )
	{
		LOG( "Stream ribbons: shader compilation failed" );
		return false;
	}
	m_phase = m_program.createPhase( 0, 0 );
	if( m_phase < 0 )
		return false;
	m_indexCount = static_cast<uint32_t>( indices.size() );
	LOG( "Stream ribbons: " + std::to_string( streams.size() ) + " streams, " + std::to_string( vertices.size() ) + " vertices" );
	return true;
}

void StreamRibbons::warmPipelines( const PassStates& states )
{
	if( empty() )
		return;
	// Лента смотрит вверх, но извилины могут вывернуть треугольник — без отсечения граней
	m_program.warmPipelines( { { RasterState::noCulling, DepthState::readOnly, BlendState::alpha },
							   { RasterState::wireframe, DepthState::readOnly, BlendState::alpha } },
							 states.scene, { m_phase } );
}

void StreamRibbons::render( const RenderContext& context )
{
	if( empty() )
		return;
	DMD3D& d3d = DMD3D::instance();
	ScopedRenderState state( context.frameRaster == RasterState::wireframe ? RasterState::wireframe : RasterState::noCulling );
	d3d.setVertexBuffer( m_vertexBuffer, sizeof( Vertex ) );
	d3d.setIndexBuffer( m_indexBuffer, DXGI_FORMAT_R32_UINT );
	m_program.setPass( m_phase );
	d3d.drawIndexed( m_indexCount, 0, 0 );
}

}
