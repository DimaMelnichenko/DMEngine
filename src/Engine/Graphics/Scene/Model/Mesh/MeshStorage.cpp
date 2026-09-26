#include "MeshStorage.h"
#include <cmath>
#include "Logger\Logger.h"

namespace
{

using Vertices = std::vector<GS::VertexData::PTNTB>;
using Indices = std::vector<uint32_t>;

// Все примитивы вписаны в куб от -0.5 до 0.5. Вершины треугольника обходятся по часовой стрелке, если смотреть
// на лицевую сторону: так лицевые грани задаёт растеризатор по умолчанию. tangent — направление роста u,
// binormal — направление роста v текстурных координат

// Четырёхугольник a, b, c, d по часовой стрелке — два треугольника
void addQuad( Indices& indices, uint32_t a, uint32_t b, uint32_t c, uint32_t d )
{
	indices.insert( indices.end(), { a, b, c, a, c, d } );
}

void buildBox( Vertices& vertices, Indices& indices )
{
	// Грань задаётся нормалью и осями u, v, где u x v = normal
	struct Face
	{
		XMFLOAT3 normal;
		XMFLOAT3 u;
		XMFLOAT3 v;
	};
	const Face faces[] =
	{
		{ {  1.0f,  0.0f,  0.0f }, {  0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f,  0.0f } },
		{ { -1.0f,  0.0f,  0.0f }, {  0.0f, 0.0f,  1.0f }, { 0.0f, 1.0f,  0.0f } },
		{ {  0.0f,  1.0f,  0.0f }, {  1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f } },
		{ {  0.0f, -1.0f,  0.0f }, {  1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f,  1.0f } },
		{ {  0.0f,  0.0f,  1.0f }, {  1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f } },
		{ {  0.0f,  0.0f, -1.0f }, { -1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f } },
	};
	const float corners[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };

	for( const Face& face : faces )
	{
		XMVECTOR normal = XMLoadFloat3( &face.normal );
		XMVECTOR u = XMLoadFloat3( &face.u );
		XMVECTOR v = XMLoadFloat3( &face.v );

		uint32_t base = static_cast<uint32_t>( vertices.size() );
		for( const auto& corner : corners )
		{
			GS::VertexData::PTNTB vertex;
			XMStoreFloat3( &vertex.position, ( normal + u * corner[0] + v * corner[1] ) * 0.5f );
			vertex.texture = XMFLOAT2( ( corner[0] + 1.0f ) * 0.5f, ( 1.0f - corner[1] ) * 0.5f );
			vertex.normal = face.normal;
			vertex.tangent = face.u;
			XMStoreFloat3( &vertex.binormal, -v );
			vertices.push_back( vertex );
		}
		addQuad( indices, base, base + 1, base + 2, base + 3 );
	}
}

// UV-сфера: u по долготе, v от северного полюса к южному
void buildSphere( Vertices& vertices, Indices& indices )
{
	const uint32_t segments = 32;
	const uint32_t rings = 16;

	for( uint32_t ring = 0; ring <= rings; ++ring )
	{
		const float theta = XM_PI * ring / rings;
		for( uint32_t segment = 0; segment <= segments; ++segment )
		{
			const float phi = XM_2PI * segment / segments;

			GS::VertexData::PTNTB vertex;
			vertex.normal = XMFLOAT3( std::sin( theta ) * std::cos( phi ), std::cos( theta ), std::sin( theta ) * std::sin( phi ) );
			vertex.position = XMFLOAT3( vertex.normal.x * 0.5f, vertex.normal.y * 0.5f, vertex.normal.z * 0.5f );
			vertex.texture = XMFLOAT2( static_cast<float>( segment ) / segments, static_cast<float>( ring ) / rings );
			vertex.tangent = XMFLOAT3( -std::sin( phi ), 0.0f, std::cos( phi ) );
			vertex.binormal = XMFLOAT3( std::cos( theta ) * std::cos( phi ), -std::sin( theta ), std::cos( theta ) * std::sin( phi ) );
			vertices.push_back( vertex );
		}
	}

	const uint32_t stride = segments + 1;
	for( uint32_t ring = 0; ring < rings; ++ring )
	{
		for( uint32_t segment = 0; segment < segments; ++segment )
		{
			const uint32_t a = ring * stride + segment;
			addQuad( indices, a, a + 1, a + stride + 1, a + stride );
		}
	}
}

// Плоскость в XZ нормалью вверх, сетка cells × cells, чтобы и повершинное освещение на большой плоскости было плавным
void buildPlane( Vertices& vertices, Indices& indices )
{
	const uint32_t cells = 8;

	for( uint32_t row = 0; row <= cells; ++row )
	{
		for( uint32_t column = 0; column <= cells; ++column )
		{
			const float u = static_cast<float>( column ) / cells;
			const float v = static_cast<float>( row ) / cells;

			GS::VertexData::PTNTB vertex;
			vertex.position = XMFLOAT3( u - 0.5f, 0.0f, 0.5f - v );
			vertex.texture = XMFLOAT2( u, v );
			vertex.normal = XMFLOAT3( 0.0f, 1.0f, 0.0f );
			vertex.tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
			vertex.binormal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
			vertices.push_back( vertex );
		}
	}

	const uint32_t stride = cells + 1;
	for( uint32_t row = 0; row < cells; ++row )
	{
		for( uint32_t column = 0; column < cells; ++column )
		{
			const uint32_t a = row * stride + column;
			addQuad( indices, a, a + 1, a + stride + 1, a + stride );
		}
	}
}

// Вертикальная карточка для травы: квадрат в плоскости XY от -0.5 до 0.5 по X и от 0 до 1 по Y, стоит на y = 0
// лицом к -Z. Трава рисуется без отсечения граней, поэтому видна с обеих сторон
void buildCard( Vertices& vertices, Indices& indices )
{
	const float corners[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };	// u, v

	for( const auto& corner : corners )
	{
		GS::VertexData::PTNTB vertex;
		vertex.position = XMFLOAT3( corner[0] - 0.5f, 1.0f - corner[1], 0.0f );
		vertex.texture = XMFLOAT2( corner[0], corner[1] );
		vertex.normal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
		vertex.tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
		vertex.binormal = XMFLOAT3( 0.0f, -1.0f, 0.0f );
		vertices.push_back( vertex );
	}
	addQuad( indices, 0, 1, 2, 3 );
}

}

namespace GS
{

MeshStorage::MeshStorage( const std::string& path ) : DMResourceStorage( path ),
	m_vertexCount(0),
	m_indexCount(0)
{

}

MeshStorage::~MeshStorage()
{

}

uint32_t MeshStorage::vertexCount() const
{
	return m_vertexCount;
}

uint32_t MeshStorage::indexCount() const
{
	return m_indexCount;
}

bool MeshStorage::primitiveFromName( const std::string& name, Primitive& primitive )
{
	if( name == "box" )
		primitive = Primitive::box;
	else if( name == "sphere" )
		primitive = Primitive::sphere;
	else if( name == "plane" )
		primitive = Primitive::plane;
	else if( name == "card" )
		primitive = Primitive::card;
	else
		return false;

	return true;
}

bool MeshStorage::createPrimitive( uint32_t id, const std::string& name, Primitive primitive )
{
	if( exists( id ) )
		return true;

	Vertices vertices;
	Indices indices;
	switch( primitive )
	{
		case Primitive::box:
			buildBox( vertices, indices );
			break;
		case Primitive::sphere:
			buildSphere( vertices, indices );
			break;
		case Primitive::plane:
			buildPlane( vertices, indices );
			break;
		case Primitive::card:
			buildCard( vertices, indices );
			break;
	}

	std::unique_ptr<AbstractMesh> mesh( new DMMesh<VertexData::PTNTB>( id, name, std::move( indices ), std::move( vertices ) ) );
	m_vertexCount += mesh->vertexCount();
	m_indexCount += mesh->indexCount();
	return insertResource( std::move( mesh ) );
}

bool MeshStorage::createPlaceholder()
{
	return createPrimitive( placeholderId, "placeholder", Primitive::box );
}

bool MeshStorage::load( uint32_t id, const std::string& name, const std::string& file )
{

	if( exists( name ) || exists( id ) )
		return true;

	std::string fullPath = path() + "\\" + file;

	try
	{
		std::unique_ptr<AbstractMesh> mesh;
		mesh.reset( m_meshLoader.loadFromFile<VertexData::PTNTB>( id, name, fullPath ) );
		m_vertexCount += mesh->vertexCount();
		m_indexCount += mesh->indexCount();
		insertResource( std::move( mesh ) );
	}
	catch( std::exception& e )
	{
		LOG( "Can`t load mesh from file: " + fullPath );
		return false;
	}

	return true;
}

}
