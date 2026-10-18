#include "PhysicsWorld.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>
#include "Logger\Logger.h"
#include "Terrain\TerrainHeightSource.h"

using namespace DirectX;

namespace GS
{

namespace
{

// Слои: неподвижное (рельеф, модели) с неподвижным не сталкивается
namespace Layers
{
constexpr JPH::ObjectLayer nonMoving = 0;
constexpr JPH::ObjectLayer moving = 1;
}

namespace BroadPhaseLayers
{
constexpr JPH::BroadPhaseLayer nonMoving( 0 );
constexpr JPH::BroadPhaseLayer moving( 1 );
constexpr uint32_t count = 2;
}

class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter
{
public:
	bool ShouldCollide( JPH::ObjectLayer a, JPH::ObjectLayer b ) const override
	{
		return a == Layers::moving || b == Layers::moving;
	}
};

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface
{
public:
	JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::count; }
	JPH::BroadPhaseLayer GetBroadPhaseLayer( JPH::ObjectLayer layer ) const override
	{
		return layer == Layers::nonMoving ? BroadPhaseLayers::nonMoving : BroadPhaseLayers::moving;
	}
#if defined( JPH_EXTERNAL_PROFILE ) || defined( JPH_PROFILE_ENABLED )
	const char* GetBroadPhaseLayerName( JPH::BroadPhaseLayer layer ) const override
	{
		return layer == BroadPhaseLayers::nonMoving ? "NonMoving" : "Moving";
	}
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
	bool ShouldCollide( JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase ) const override
	{
		return layer == Layers::moving || broadPhase == BroadPhaseLayers::moving;
	}
};

// Сообщения Jolt — в лог движка
void trace( const char* format, ... )
{
	char text[1024];
	va_list args;
	va_start( args, format );
	std::vsnprintf( text, sizeof( text ), format, args );
	va_end( args );
	LOG( std::string( "Jolt: " ) + text );
}

// Типы Jolt (фабрика сериализации, формы) — один раз на процесс
void registerJolt()
{
	static const bool registered = []
	{
		JPH::RegisterDefaultAllocator();
		JPH::Trace = trace;
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
		return true;
	}();
	(void)registered;
}

JPH::Vec3 vec3( const XMFLOAT3& v )
{
	return JPH::Vec3( v.x, v.y, v.z );
}

XMFLOAT3 float3( JPH::Vec3Arg v )
{
	return XMFLOAT3( v.GetX(), v.GetY(), v.GetZ() );
}

// Мировая матрица → масштаб (в форму), поворот и место тела
struct Placement
{
	XMFLOAT3 scale;
	JPH::Quat rotation;
	JPH::Vec3 position;
	bool mirrored;
};

Placement decompose( FXMMATRIX world )
{
	XMVECTOR scale, rotation, translation;
	XMMatrixDecompose( &scale, &rotation, &translation, world );
	Placement placement;
	XMStoreFloat3( &placement.scale, scale );
	XMFLOAT4 q;
	XMStoreFloat4( &q, XMQuaternionNormalize( rotation ) );
	placement.rotation = JPH::Quat( q.x, q.y, q.z, q.w );
	XMFLOAT3 t;
	XMStoreFloat3( &t, translation );
	placement.position = vec3( t );
	placement.mirrored = XMVectorGetX( XMMatrixDeterminant( world ) ) < 0.0f;
	return placement;
}

XMFLOAT3 scaled( const XMFLOAT3& v, const XMFLOAT3& scale )
{
	return XMFLOAT3( v.x * scale.x, v.y * scale.y, v.z * scale.z );
}

JPH::RefConst<JPH::Shape> create( const JPH::ShapeSettings& settings, const char* what )
{
	const JPH::ShapeSettings::ShapeResult result = settings.Create();
	if( result.HasError() )
	{
		LOG( std::string( "Physics: " ) + what + " shape is not built: " + result.GetError().c_str() );
		return nullptr;
	}
	return result.Get();
}

// Ключ тела Jolt — для обратной карты (попадание луча → своё тело)
uint32_t bodyKey( const JPH::BodyID& body )
{
	return body.GetIndexAndSequenceNumber();
}

#ifdef JPH_DEBUG_RENDERER
// Отладочный рендер Jolt, который не рисует, а собирает отрезки: треугольники форм — тремя рёбрами. Единственный на процесс
// (DebugRenderer::sInstance)
class LineCollector final : public JPH::DebugRendererSimple
{
public:
	void DrawLine( JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg ) override
	{
		lines->push_back( { float3( from ), float3( to ), character } );
	}
	void DrawTriangle( JPH::RVec3Arg a, JPH::RVec3Arg b, JPH::RVec3Arg c, JPH::ColorArg color, ECastShadow ) override
	{
		DrawLine( a, b, color );
		DrawLine( b, c, color );
		DrawLine( c, a, color );
	}
	void DrawText3D( JPH::RVec3Arg, const std::string_view&, JPH::ColorArg, float ) override {}

	std::vector<PhysicsWorld::DebugLine>* lines = nullptr;
	bool character = false;
};

// Тела ближе радиуса к точке, кроме земли
class NearBodies final : public JPH::BodyDrawFilter
{
public:
	NearBodies( JPH::Vec3Arg center, float radius, JPH::BodyID terrain ) : m_center( center ), m_radiusSq( radius * radius ), m_terrain( terrain ) {}
	bool ShouldDraw( const JPH::Body& body ) const override
	{
		return body.GetID() != m_terrain && body.GetWorldSpaceBounds().GetSqDistanceTo( m_center ) <= m_radiusSq;
	}

private:
	JPH::Vec3 m_center;
	float m_radiusSq;
	JPH::BodyID m_terrain;
};
#endif

}

struct PhysicsWorld::Impl
{
	struct Character
	{
		JPH::Ref<JPH::CharacterVirtual> body;
		CharacterSettings settings;
	};

	Impl() : tempAllocator( 16 * 1024 * 1024 )
	{
		system.Init( 65536, 0, 65536, 10240, broadPhaseLayers, objectVsBroadPhase, objectLayerPairs );
	}

	~Impl()
	{
		for( auto& [id, character] : characters )
			characterVsCharacter.Remove( character.body );
		characters.clear();
		JPH::BodyInterface& bodies = system.GetBodyInterface();
		for( auto& [id, body] : staticBodies )
		{
			bodies.RemoveBody( body );
			bodies.DestroyBody( body );
		}
		if( !terrainBody.IsInvalid() )
		{
			bodies.RemoveBody( terrainBody );
			bodies.DestroyBody( terrainBody );
		}
	}

	BodyId addStatic( const JPH::RefConst<JPH::Shape>& shape, const Placement& placement )
	{
		if( !shape )
			return 0;
		JPH::BodyCreationSettings settings( shape, placement.position, placement.rotation, JPH::EMotionType::Static, Layers::nonMoving );
		const JPH::BodyID body = system.GetBodyInterface().CreateAndAddBody( settings, JPH::EActivation::DontActivate );
		if( body.IsInvalid() )
			return 0;
		const BodyId id = nextBody++;
		staticBodies[id] = body;
		bodyIds[bodyKey( body )] = id;
		return id;
	}

	ShapeId addShape( const JPH::RefConst<JPH::Shape>& shape )
	{
		if( !shape )
			return 0;
		const ShapeId id = nextShape++;
		shapes[id] = shape;
		return id;
	}

	// Коробка по границам точек — для плоской или вырожденной оболочки
	static JPH::RefConst<JPH::Shape> boundsBox( const std::vector<XMFLOAT3>& points )
	{
		XMFLOAT3 low( FLT_MAX, FLT_MAX, FLT_MAX ), high( -FLT_MAX, -FLT_MAX, -FLT_MAX );
		for( const XMFLOAT3& p : points )
		{
			low = XMFLOAT3( std::min( low.x, p.x ), std::min( low.y, p.y ), std::min( low.z, p.z ) );
			high = XMFLOAT3( std::max( high.x, p.x ), std::max( high.y, p.y ), std::max( high.z, p.z ) );
		}
		const JPH::Vec3 half( std::max( 0.5f * ( high.x - low.x ), 0.01f ), std::max( 0.5f * ( high.y - low.y ), 0.01f ),
							  std::max( 0.5f * ( high.z - low.z ), 0.01f ) );
		const JPH::Vec3 center( 0.5f * ( low.x + high.x ), 0.5f * ( low.y + high.y ), 0.5f * ( low.z + high.z ) );
		return create( JPH::RotatedTranslatedShapeSettings( center, JPH::Quat::sIdentity(),
															 new JPH::BoxShapeSettings( half, std::min( JPH::cDefaultConvexRadius, half.ReduceMin() ) ) ),
					   "hull box" );
	}

	// Капсула персонажа: стоит на точке ног
	JPH::RefConst<JPH::Shape> characterShape( const CharacterSettings& settings ) const
	{
		const float radius = std::max( settings.radius, 0.05f );
		const float halfCylinder = std::max( 0.5f * settings.height - radius, 0.01f );
		return create( JPH::RotatedTranslatedShapeSettings( JPH::Vec3( 0.0f, halfCylinder + radius, 0.0f ), JPH::Quat::sIdentity(),
															 new JPH::CapsuleShape( halfCylinder, radius ) ), "character" );
	}

	JPH::Ref<JPH::CharacterVirtual> makeCharacter( const CharacterSettings& settings, JPH::Vec3Arg feet )
	{
		JPH::Ref<JPH::CharacterVirtualSettings> virtualSettings = new JPH::CharacterVirtualSettings();
		virtualSettings->mShape = characterShape( settings );
		virtualSettings->mMaxSlopeAngle = JPH::DegreesToRadians( settings.maxSlope );
		virtualSettings->mUp = JPH::Vec3::sAxisY();
		// Опора — только нижняя полусфера капсулы: касание боком — не земля
		virtualSettings->mSupportingVolume = JPH::Plane( JPH::Vec3::sAxisY(), -std::max( settings.radius, 0.05f ) );
		JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual( virtualSettings, feet, JPH::Quat::sIdentity(), 0, &system );
		character->SetCharacterVsCharacterCollision( &characterVsCharacter );
		characterVsCharacter.Add( character );
		return character;
	}

	// Глубина воды в точке, м (0 — воды нет или земли нет)
	float waterDepth( float x, float z ) const
	{
		float level = 0.0f, ground = 0.0f;
		if( !terrain || !terrain->waterLevel( x, z, level ) || !terrain->surfaceHeight( x, z, ground ) )
			return 0.0f;
		return std::max( level - ground, 0.0f );
	}

	// Желаемая скорость без шага в воду глубже предела: составляющая к большей глубине убирается — у берега идёшь вдоль него
	JPH::Vec3 limitByWater( JPH::Vec3Arg desired, JPH::Vec3Arg feet, const CharacterSettings& settings ) const
	{
		const float speed = desired.Length();
		if( !terrain || settings.maxWadeDepth <= 0.0f || speed < 1e-4f )
			return desired;
		const JPH::Vec3 direction = desired / speed;
		const JPH::Vec3 probe = feet + direction * ( settings.radius + 0.25f );
		if( waterDepth( probe.GetX(), probe.GetZ() ) <= settings.maxWadeDepth )
			return desired;
		// Куда глубже — по разности глубин вокруг пробы
		constexpr float d = 0.5f;
		JPH::Vec3 deeper( waterDepth( probe.GetX() + d, probe.GetZ() ) - waterDepth( probe.GetX() - d, probe.GetZ() ), 0.0f,
						  waterDepth( probe.GetX(), probe.GetZ() + d ) - waterDepth( probe.GetX(), probe.GetZ() - d ) );
		deeper = deeper.LengthSq() > 1e-8f ? deeper.Normalized() : direction;
		const float along = desired.Dot( deeper );
		return along > 0.0f ? desired - deeper * along : desired;
	}

	BroadPhaseLayerMap broadPhaseLayers;
	ObjectVsBroadPhase objectVsBroadPhase;
	ObjectLayerPairs objectLayerPairs;
	JPH::TempAllocatorImpl tempAllocator;
	JPH::PhysicsSystem system;
	JPH::CharacterVsCharacterCollisionSimple characterVsCharacter;

	const TerrainHeightSource* terrain = nullptr;
	JPH::BodyID terrainBody;
	uint32_t terrainRevision = ~0u;
	std::unordered_map<BodyId, JPH::BodyID> staticBodies;
	std::unordered_map<uint32_t, BodyId> bodyIds;		// тело Jolt (bodyKey) → своё
	BodyId nextBody = 1;
	std::unordered_map<ShapeId, JPH::RefConst<JPH::Shape>> shapes;	// общие формы (createShape): держит их, пока мир есть
	ShapeId nextShape = 1;
	std::unordered_map<CharacterId, Character> characters;
	CharacterId nextCharacter = 1;
};

PhysicsWorld::PhysicsWorld()
{
	registerJolt();
	m_impl = std::make_unique<Impl>();
}

PhysicsWorld::~PhysicsWorld() = default;

bool PhysicsWorld::buildTerrain( const TerrainHeightSource& terrain )
{
	Impl& impl = *m_impl;
	const TerrainHeight height = terrain.terrainHeight();
	float probe = 0.0f;
	if( height.mapSize == 0 || height.worldSize <= 0.0f || !terrain.surfaceHeight( 0.5f * height.worldSize, 0.5f * height.worldSize, probe ) )
		return false;

	JPH::BodyInterface& bodies = impl.system.GetBodyInterface();
	if( !impl.terrainBody.IsInvalid() )
	{
		bodies.RemoveBody( impl.terrainBody );
		bodies.DestroyBody( impl.terrainBody );
		impl.terrainBody = JPH::BodyID();
	}
	impl.terrain = &terrain;

	// Карта высот Jolt: число выборок — кратное блоку (4), лишние — дыры
	constexpr uint32_t block = 4;
	const float noCollision = JPH::HeightFieldShapeConstants::cNoCollisionValue;
	auto heightField = [&]( float x0, float z0, float spacing, uint32_t cells, const auto& isHole ) -> JPH::RefConst<JPH::Shape>
	{
		const uint32_t samples = ( cells + 1 + block - 1 ) / block * block;
		std::vector<float> values( static_cast<size_t>( samples ) * samples, noCollision );
		for( uint32_t j = 0; j <= cells; ++j )
			for( uint32_t i = 0; i <= cells; ++i )
			{
				const float x = x0 + i * spacing;
				const float z = z0 + j * spacing;
				float value = 0.0f;
				if( !isHole( i, j ) && terrain.surfaceHeight( x, z, value ) )
					values[static_cast<size_t>( j ) * samples + i] = value;
			}
		JPH::HeightFieldShapeSettings settings( values.data(), JPH::Vec3( x0, 0.0f, z0 ), JPH::Vec3( spacing, 1.0f, spacing ), samples );
		settings.mBlockSize = block;
		return create( settings, "terrain height field" );
	};

	// Главная карта — в узлах текселя; узлы строго внутри детальных квадратов — дыры: их треугольники уходят, остаются
	// треугольники квадратов
	const uint32_t cells = height.mapSize;
	const float texel = height.worldSize / height.mapSize;
	const std::vector<TerrainHeightSource::FineRegion> regions = terrain.fineRegions();
	std::vector<uint8_t> hole( static_cast<size_t>( cells + 1 ) * ( cells + 1 ), 0 );
	for( const TerrainHeightSource::FineRegion& region : regions )
	{
		const int i0 = static_cast<int>( std::lround( region.x / texel ) );
		const int j0 = static_cast<int>( std::lround( region.z / texel ) );
		const int n = static_cast<int>( std::lround( region.size / texel ) );
		for( int j = j0 + 1; j < j0 + n; ++j )
			for( int i = i0 + 1; i < i0 + n; ++i )
				if( i >= 0 && j >= 0 && i <= static_cast<int>( cells ) && j <= static_cast<int>( cells ) )
					hole[static_cast<size_t>( j ) * ( cells + 1 ) + i] = 1;
	}
	JPH::StaticCompoundShapeSettings compound;
	const JPH::RefConst<JPH::Shape> main = heightField( 0.0f, 0.0f, texel, cells, [&]( uint32_t i, uint32_t j )
	{
		return hole[static_cast<size_t>( j ) * ( cells + 1 ) + i] != 0;
	} );
	if( !main )
		return false;
	compound.AddShape( JPH::Vec3::sZero(), JPH::Quat::sIdentity(), main );
	for( const TerrainHeightSource::FineRegion& region : regions )
	{
		const uint32_t fine = static_cast<uint32_t>( std::lround( region.size / region.spacing ) );
		const JPH::RefConst<JPH::Shape> tile = heightField( region.x, region.z, region.spacing, fine, []( uint32_t, uint32_t ) { return false; } );
		if( tile )
			compound.AddShape( JPH::Vec3::sZero(), JPH::Quat::sIdentity(), tile );
	}
	const JPH::RefConst<JPH::Shape> shape = create( compound, "terrain" );
	if( !shape )
		return false;
	JPH::BodyCreationSettings settings( shape, JPH::Vec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::nonMoving );
	impl.terrainBody = bodies.CreateAndAddBody( settings, JPH::EActivation::DontActivate );
	impl.terrainRevision = height.revision;
	return !impl.terrainBody.IsInvalid();
}

uint32_t PhysicsWorld::terrainRevision() const
{
	return m_impl->terrainBody.IsInvalid() ? ~0u : m_impl->terrainRevision;
}

PhysicsWorld::ShapeId PhysicsWorld::createBoxShape( const XMFLOAT3& center, const XMFLOAT3& halfExtents )
{
	const JPH::Vec3 half( std::max( std::abs( halfExtents.x ), 0.01f ), std::max( std::abs( halfExtents.y ), 0.01f ),
						  std::max( std::abs( halfExtents.z ), 0.01f ) );
	return m_impl->addShape( create( JPH::RotatedTranslatedShapeSettings( vec3( center ), JPH::Quat::sIdentity(),
																		  new JPH::BoxShapeSettings( half, std::min( JPH::cDefaultConvexRadius, half.ReduceMin() ) ) ),
									 "box" ) );
}

PhysicsWorld::ShapeId PhysicsWorld::createHullShape( const std::vector<XMFLOAT3>& points )
{
	if( points.empty() )
		return 0;
	JPH::Array<JPH::Vec3> hull;
	hull.reserve( points.size() );
	for( const XMFLOAT3& point : points )
		hull.push_back( vec3( point ) );
	const JPH::ShapeSettings::ShapeResult result = JPH::ConvexHullShapeSettings( hull ).Create();
	// Плоская или вырожденная оболочка — коробкой по границам
	return m_impl->addShape( result.HasError() ? Impl::boundsBox( points ) : result.Get() );
}

PhysicsWorld::ShapeId PhysicsWorld::createMeshShape( const std::vector<XMFLOAT3>& vertices, const std::vector<uint32_t>& indices )
{
	JPH::VertexList list;
	list.reserve( vertices.size() );
	for( const XMFLOAT3& vertex : vertices )
		list.push_back( JPH::Float3( vertex.x, vertex.y, vertex.z ) );
	JPH::IndexedTriangleList triangles;
	triangles.reserve( indices.size() / 3 );
	for( size_t i = 0; i + 2 < indices.size(); i += 3 )
		triangles.push_back( JPH::IndexedTriangle( indices[i], indices[i + 1], indices[i + 2] ) );
	return m_impl->addShape( create( JPH::MeshShapeSettings( list, triangles ), "mesh" ) );
}

PhysicsWorld::ShapeId PhysicsWorld::createCapsuleShape( const XMFLOAT3& base, float height, float radius )
{
	const float r = std::max( radius, 0.02f );
	const float h = std::max( height, 2.0f * r + 0.02f );
	return m_impl->addShape( create( JPH::RotatedTranslatedShapeSettings( JPH::Vec3( base.x, base.y + 0.5f * h, base.z ), JPH::Quat::sIdentity(),
																		  new JPH::CapsuleShapeSettings( 0.5f * h - r, r ) ),
									 "capsule" ) );
}

PhysicsWorld::BodyId PhysicsWorld::addStaticBody( ShapeId shapeId, FXMMATRIX world )
{
	auto it = m_impl->shapes.find( shapeId );
	if( it == m_impl->shapes.end() )
		return 0;
	const Placement placement = decompose( world );
	JPH::RefConst<JPH::Shape> shape = it->second;
	// Масштаб экземпляра — оболочкой ScaledShape над общей формой; там, где форма не допускает (неравномерный у капсулы), —
	// ближайший допустимый. Зеркальный (отрицательный) — тоже масштабом: Jolt выворачивает форму сам
	JPH::Vec3 scale = vec3( placement.scale );
	if( !scale.IsClose( JPH::Vec3::sOne(), 1e-8f ) )
	{
		if( !shape->IsValidScale( scale ) )
			scale = shape->MakeScaleValid( scale );
		shape = new JPH::ScaledShape( shape, scale );
	}
	return m_impl->addStatic( shape, placement );
}

PhysicsWorld::BodyId PhysicsWorld::addStaticBox( const XMFLOAT3& center, const XMFLOAT3& halfExtents, FXMMATRIX world )
{
	const Placement placement = decompose( world );
	const XMFLOAT3 c = scaled( center, placement.scale );
	// Масштаб — в размеры: плоская модель (плоскость) — коробка в сантиметр толщиной при любом масштабе; скругление краёв —
	// не толще коробки
	const JPH::Vec3 half( std::max( std::abs( halfExtents.x * placement.scale.x ), 0.01f ),
						  std::max( std::abs( halfExtents.y * placement.scale.y ), 0.01f ),
						  std::max( std::abs( halfExtents.z * placement.scale.z ), 0.01f ) );
	const float radius = std::min( JPH::cDefaultConvexRadius, half.ReduceMin() );
	const JPH::RefConst<JPH::Shape> shape = create( JPH::RotatedTranslatedShapeSettings( vec3( c ), JPH::Quat::sIdentity(),
																						  new JPH::BoxShapeSettings( half, radius ) ), "box" );
	return m_impl->addStatic( shape, placement );
}

PhysicsWorld::BodyId PhysicsWorld::addStaticHull( const std::vector<XMFLOAT3>& points, FXMMATRIX world )
{
	return addStaticBody( createHullShape( points ), world );
}

PhysicsWorld::BodyId PhysicsWorld::addStaticMesh( const std::vector<XMFLOAT3>& vertices, const std::vector<uint32_t>& indices,
												 FXMMATRIX world )
{
	return addStaticBody( createMeshShape( vertices, indices ), world );
}

PhysicsWorld::BodyId PhysicsWorld::addStaticCapsule( const XMFLOAT3& base, float height, float radius, FXMMATRIX world )
{
	return addStaticBody( createCapsuleShape( base, height, radius ), world );
}

void PhysicsWorld::removeBody( BodyId body )
{
	auto it = m_impl->staticBodies.find( body );
	if( it == m_impl->staticBodies.end() )
		return;
	JPH::BodyInterface& bodies = m_impl->system.GetBodyInterface();
	m_impl->bodyIds.erase( bodyKey( it->second ) );
	bodies.RemoveBody( it->second );
	bodies.DestroyBody( it->second );
	m_impl->staticBodies.erase( it );
}

size_t PhysicsWorld::bodyCount() const
{
	return m_impl->staticBodies.size();
}

void PhysicsWorld::optimize()
{
	m_impl->system.OptimizeBroadPhase();
}

bool PhysicsWorld::castRay( const XMFLOAT3& origin, const XMFLOAT3& direction, float& fraction, BodyId* body ) const
{
	const JPH::RRayCast ray( vec3( origin ), vec3( direction ) );
	JPH::RayCastResult hit;
	if( !m_impl->system.GetNarrowPhaseQuery().CastRay( ray, hit ) )
		return false;
	fraction = hit.mFraction;
	if( body )
	{
		auto it = m_impl->bodyIds.find( bodyKey( hit.mBodyID ) );
		*body = it != m_impl->bodyIds.end() ? it->second : 0;
	}
	return true;
}

void PhysicsWorld::debugLines( const XMFLOAT3& center, float radius, std::vector<DebugLine>& lines ) const
{
#ifdef JPH_DEBUG_RENDERER
	static LineCollector collector;
	collector.lines = &lines;
	collector.SetCameraPos( vec3( center ) );
	// Неподвижные тела рядом — каркасом форм
	collector.character = false;
	JPH::BodyManager::DrawSettings settings;
	settings.mDrawShape = true;
	settings.mDrawShapeWireframe = true;
	const NearBodies filter( vec3( center ), radius, m_impl->terrainBody );
	m_impl->system.DrawBodies( settings, &collector, &filter );
	// Капсулы персонажей
	collector.character = true;
	for( const auto& [id, character] : m_impl->characters )
	{
		const JPH::CharacterVirtual& body = *character.body;
		const JPH::Vec3 offset = vec3( center ) - body.GetPosition();
		if( offset.LengthSq() > radius * radius )
			continue;
		// Камера внутри капсулы (идущий) — изнутри каркас только мешает
		const float side = offset.GetX() * offset.GetX() + offset.GetZ() * offset.GetZ();
		if( side < character.settings.radius * character.settings.radius && offset.GetY() >= 0.0f && offset.GetY() <= character.settings.height )
			continue;
		body.GetShape()->Draw( &collector, body.GetCenterOfMassTransform(), JPH::Vec3::sOne(), JPH::Color::sYellow, false, true );
	}
	collector.lines = nullptr;
#else
	( void )center;
	( void )radius;
	( void )lines;
#endif
}

PhysicsWorld::CharacterId PhysicsWorld::createCharacter( const CharacterSettings& settings, const XMFLOAT3& feet )
{
	Impl::Character character;
	character.settings = settings;
	character.body = m_impl->makeCharacter( settings, vec3( feet ) );
	const CharacterId id = m_impl->nextCharacter++;
	m_impl->characters[id] = std::move( character );
	return id;
}

void PhysicsWorld::setCharacterSettings( CharacterId id, const CharacterSettings& settings )
{
	auto it = m_impl->characters.find( id );
	if( it == m_impl->characters.end() )
		return;
	Impl::Character& character = it->second;
	const JPH::RVec3 feet = character.body->GetPosition();
	const JPH::Vec3 velocity = character.body->GetLinearVelocity();
	m_impl->characterVsCharacter.Remove( character.body );
	character.settings = settings;
	character.body = m_impl->makeCharacter( settings, feet );
	character.body->SetLinearVelocity( velocity );
}

PhysicsWorld::CharacterState PhysicsWorld::moveCharacter( CharacterId id, const CharacterMove& move, float seconds )
{
	auto it = m_impl->characters.find( id );
	if( it == m_impl->characters.end() || seconds <= 0.0f )
		return characterState( id );
	Impl& impl = *m_impl;
	JPH::CharacterVirtual& character = *it->second.body;
	const CharacterSettings& settings = it->second.settings;
	const JPH::Vec3 up = JPH::Vec3::sAxisY();
	const JPH::Vec3 gravity( 0.0f, -move.gravity, 0.0f );

	// Как в примере CharacterVirtual Jolt: на земле — скорость земли (вертикаль своя сбрасывается), в воздухе — своя
	// вертикаль; гравитация; затем желаемая по горизонтали без шага в крутой склон и в глубокую воду
	character.UpdateGroundVelocity();
	const JPH::Vec3 vertical = up * up.Dot( character.GetLinearVelocity() );
	const JPH::Vec3 groundVelocity = character.GetGroundVelocity();
	const bool onGround = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
	JPH::Vec3 velocity = onGround && ( vertical - groundVelocity ).Dot( up ) < 0.1f ? groundVelocity : vertical;
	if( onGround && move.jumpSpeed > 0.0f )
		velocity += up * move.jumpSpeed;
	velocity += gravity * seconds;
	JPH::Vec3 desired( move.velocity.x, 0.0f, move.velocity.y );
	desired = impl.limitByWater( desired, character.GetPosition(), settings );
	desired = character.CancelVelocityTowardsSteepSlopes( desired );
	character.SetLinearVelocity( velocity + desired );

	JPH::CharacterVirtual::ExtendedUpdateSettings update;
	update.mStickToFloorStepDown = JPH::Vec3( 0.0f, -settings.stepHeight, 0.0f );
	update.mWalkStairsStepUp = JPH::Vec3( 0.0f, settings.stepHeight, 0.0f );
	character.ExtendedUpdate( seconds, gravity, update, impl.system.GetDefaultBroadPhaseLayerFilter( Layers::moving ),
							  impl.system.GetDefaultLayerFilter( Layers::moving ), {}, {}, impl.tempAllocator );
	return characterState( id );
}

void PhysicsWorld::setCharacterPosition( CharacterId id, const XMFLOAT3& feet )
{
	auto it = m_impl->characters.find( id );
	if( it == m_impl->characters.end() )
		return;
	it->second.body->SetPosition( vec3( feet ) );
	it->second.body->SetLinearVelocity( JPH::Vec3::sZero() );
}

PhysicsWorld::CharacterState PhysicsWorld::characterState( CharacterId id ) const
{
	CharacterState state;
	auto it = m_impl->characters.find( id );
	if( it == m_impl->characters.end() )
		return state;
	const JPH::CharacterVirtual& character = *it->second.body;
	state.feet = float3( character.GetPosition() );
	state.velocity = float3( character.GetLinearVelocity() );
	state.onGround = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
	return state;
}

void PhysicsWorld::removeCharacter( CharacterId id )
{
	auto it = m_impl->characters.find( id );
	if( it == m_impl->characters.end() )
		return;
	m_impl->characterVsCharacter.Remove( it->second.body );
	m_impl->characters.erase( it );
}

size_t PhysicsWorld::characterCount() const
{
	return m_impl->characters.size();
}

}
