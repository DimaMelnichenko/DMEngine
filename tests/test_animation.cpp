// Скелетная анимация (ядро): выборка клипа, поза в пространстве модели и матрицы скиннинга, смешение мест костей, шаг по
// скорости (BlendSpace1D), система анимации сущностей и переход между режимами, чтение файлов .skel и .anim
#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "Engine\Animation\Skeleton.h"
#include "Engine\Animation\AnimationClip.h"
#include "Engine\Animation\AnimationPose.h"
#include "Engine\Animation\BlendSpace1D.h"
#include "Engine\Animation\AnimationLibrary.h"
#include "Engine\Animation\AnimationSystem.h"
#include "Engine\Simulation\World.h"
#include "Engine\Simulation\Components.h"

using namespace GS;
using namespace DirectX;

namespace
{

constexpr float pi = 3.14159265f;

XMFLOAT4 yaw( float radians )
{
	XMFLOAT4 q;
	XMStoreFloat4( &q, XMQuaternionRotationRollPitchYaw( 0.0f, radians, 0.0f ) );
	return q;
}

// Угол поворота кватерниона вокруг вертикали
float yawOf( const XMFLOAT4& q )
{
	XMFLOAT3 forward;
	XMStoreFloat3( &forward, XMVector3Rotate( XMVectorSet( 0.0f, 0.0f, 1.0f, 0.0f ), XMLoadFloat4( &q ) ) );
	return std::atan2( forward.x, forward.z );
}

JointTransform joint( float x, float y, float z, const XMFLOAT4& rotation = XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f ) )
{
	JointTransform result;
	result.translation = XMFLOAT3( x, y, z );
	result.rotation = rotation;
	return result;
}

std::shared_ptr<Skeleton> oneJointSkeleton()
{
	auto skeleton = std::make_shared<Skeleton>();
	std::string reason;
	REQUIRE( skeleton->create( { Skeleton::Joint{ "root", -1, JointTransform() } }, reason ) );
	return skeleton;
}

// Клип одной кости: кадры — сдвиг по X
std::shared_ptr<AnimationClip> slideClip( const std::vector<float>& xs, float frameRate, bool looping, float rootSpeed )
{
	std::vector<JointTransform> frames;
	for( float x : xs )
		frames.push_back( joint( x, 0.0f, 0.0f ) );
	auto clip = std::make_shared<AnimationClip>();
	std::string reason;
	REQUIRE( clip->create( 1, frameRate, frames, looping, reason ) );
	clip->setRootMotion( rootSpeed, 0.0f );
	return clip;
}

template<typename T> void put( std::vector<uint8_t>& bytes, const T& value )
{
	const uint8_t* p = reinterpret_cast<const uint8_t*>( &value );
	bytes.insert( bytes.end(), p, p + sizeof( T ) );
}

}

TEST_CASE( "AnimationClip: frames blend linearly, a looping clip wraps, a one-shot holds the last frame" )
{
	std::vector<JointTransform> frames = { joint( 0.0f, 0.0f, 0.0f, yaw( 0.0f ) ), joint( 1.0f, 0.0f, 0.0f, yaw( pi / 2.0f ) ),
										   joint( 2.0f, 0.0f, 0.0f, yaw( pi ) ) };
	AnimationClip looping, once;
	std::string reason;
	REQUIRE( looping.create( 1, 10.0f, frames, true, reason ) );
	REQUIRE( once.create( 1, 10.0f, frames, false, reason ) );
	CHECK( looping.duration() == doctest::Approx( 0.2f ) );

	std::vector<JointTransform> pose;
	looping.sample( 0.05, pose );
	CHECK( pose[0].translation.x == doctest::Approx( 0.5f ) );
	CHECK( yawOf( pose[0].rotation ) == doctest::Approx( pi / 4.0f ).epsilon( 1e-4 ) );
	looping.sample( 0.25, pose );				// 0,25 по модулю 0,2 — 0,05
	CHECK( pose[0].translation.x == doctest::Approx( 0.5f ) );
	looping.sample( -0.15, pose );
	CHECK( pose[0].translation.x == doctest::Approx( 0.5f ) );
	once.sample( 5.0, pose );
	CHECK( pose[0].translation.x == doctest::Approx( 2.0f ) );
	once.sample( -1.0, pose );
	CHECK( pose[0].translation.x == doctest::Approx( 0.0f ) );
}

TEST_CASE( "AnimationPose: model space follows the parents, skinning is identity in the rest pose" )
{
	// Корень на высоте 1 м повёрнут на 90° (+Z к +X), кость в 1 м впереди корня
	Skeleton skeleton;
	std::string reason;
	REQUIRE( skeleton.create( { Skeleton::Joint{ "root", -1, joint( 0.0f, 1.0f, 0.0f, yaw( pi / 2.0f ) ) },
								Skeleton::Joint{ "head", 0, joint( 0.0f, 0.0f, 1.0f ) } }, reason ) );
	CHECK( skeleton.find( "head" ) == 1 );
	CHECK( skeleton.find( "tail" ) == -1 );

	std::vector<JointTransform> pose;
	restPose( skeleton, pose );
	std::vector<XMFLOAT4X4> model;
	modelSpace( skeleton, pose, model );
	CHECK( model[1]._41 == doctest::Approx( 1.0f ) );
	CHECK( model[1]._42 == doctest::Approx( 1.0f ) );
	CHECK( model[1]._43 == doctest::Approx( 0.0f ).epsilon( 1e-5 ) );

	XMFLOAT3X4 skin[2];
	skinningMatrices( skeleton, model, skin );
	for( const XMFLOAT3X4& m : skin )
		for( int row = 0; row < 3; ++row )
			for( int column = 0; column < 4; ++column )
				CHECK( m.m[row][column] == doctest::Approx( row == column ? 1.0f : 0.0f ).epsilon( 1e-5 ) );

	// Корень развернулся ещё на 180°: вершина у головы (1, 1, 0) — по другую сторону, (−1, 1, 0)
	pose[0].rotation = yaw( pi * 1.5f );
	modelSpace( skeleton, pose, model );
	skinningMatrices( skeleton, model, skin );
	const float p[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
	float out[3];
	for( int row = 0; row < 3; ++row )
		out[row] = skin[1].m[row][0] * p[0] + skin[1].m[row][1] * p[1] + skin[1].m[row][2] * p[2] + skin[1].m[row][3] * p[3];
	CHECK( out[0] == doctest::Approx( -1.0f ).epsilon( 1e-5 ) );
	CHECK( out[1] == doctest::Approx( 1.0f ) );
	CHECK( out[2] == doctest::Approx( 0.0f ).epsilon( 1e-5 ) );
}

TEST_CASE( "blendJoint: translation midway, rotation half the angle along the shortest arc" )
{
	const JointTransform a = joint( 0.0f, 0.0f, 0.0f, yaw( 0.0f ) );
	XMFLOAT4 negated = yaw( pi / 2.0f );		// −q — тот же поворот на 90°
	negated = XMFLOAT4( -negated.x, -negated.y, -negated.z, -negated.w );
	const JointTransform b = joint( 2.0f, 0.0f, 0.0f, negated );
	const JointTransform half = blendJoint( a, b, 0.5f );
	CHECK( half.translation.x == doctest::Approx( 1.0f ) );
	CHECK( yawOf( half.rotation ) == doctest::Approx( pi / 4.0f ).epsilon( 1e-4 ) );
}

TEST_CASE( "BlendSpace1D: weights by speed, the phase rate keeps the blended stride at the speed" )
{
	BlendSpace1D blend;
	blend.addSample( 3, 6.0f, 0.6f );		// рысь: шаг 3,6 м за цикл
	blend.addSample( 1, 0.0f, 5.0f );		// покой
	blend.addSample( 2, 1.5f, 1.0f );		// шаг: 1,5 м за цикл
	REQUIRE( blend.samples().size() == 3 );
	CHECK( blend.samples().front().clip == 1 );

	BlendSpace1D::Result still = blend.evaluate( 0.0f );
	CHECK( still.count == 1 );
	CHECK( still.samples[0].clip == 1 );
	CHECK_FALSE( still.samples[0].moving );
	CHECK( still.phaseRate == 0.0f );

	// Между покоем и шагом: клип на ходу — в своём темпе, цикл в секунду
	BlendSpace1D::Result slow = blend.evaluate( 0.75f );
	REQUIRE( slow.count == 2 );
	CHECK( slow.samples[0].weight == doctest::Approx( 0.5f ) );
	CHECK( slow.samples[1].clip == 2 );
	CHECK( slow.phaseRate == doctest::Approx( 1.0f ) );

	// Между шагом и рысью: смешанный шаг за цикл × циклов в секунду = скорость
	BlendSpace1D::Result mixed = blend.evaluate( 3.75f );
	REQUIRE( mixed.count == 2 );
	CHECK( mixed.samples[1].weight == doctest::Approx( 0.5f ) );
	CHECK( mixed.phaseRate * ( 0.5f * 1.5f + 0.5f * 3.6f ) == doctest::Approx( 3.75f ) );

	// Выше последней точки — рысь быстрее
	BlendSpace1D::Result fast = blend.evaluate( 9.0f );
	CHECK( fast.count == 1 );
	CHECK( fast.samples[0].clip == 3 );
	CHECK( fast.phaseRate == doctest::Approx( 2.5f ) );
}

namespace
{

// Двигает сущности по X со скоростью speed (до системы анимации, как MovementSystem)
class SlideSystem : public SimulationSystem
{
public:
	const char* name() const override { return "Slide"; }
	void step( World& world, float seconds ) override
	{
		for( auto [entity, transform] : world.registry().view<Transform>().each() )
			transform.position.x += speed * seconds;
	}
	float speed = 0.0f;
};

}

TEST_CASE( "AnimationSystem: walks by the actual speed, a blocked entity stops into idle, frozen time stops the phase" )
{
	World world;
	AnimationLibrary& library = world.animations();
	library.addSkeleton( 1, "test", oneJointSkeleton() );
	REQUIRE( library.addClip( 10, 1, "Idle", slideClip( { 0.0f, 0.0f }, 1.0f, true, 0.0f ), 0.0f ) );
	REQUIRE( library.addClip( 11, 1, "Walk", slideClip( { 0.0f, 1.0f, 0.0f }, 2.0f, true, 1.5f ), 1.5f ) );
	REQUIRE( library.addClip( 12, 1, "Lie", slideClip( { 5.0f, 7.0f }, 1.0f, false, 0.0f ), -1.0f ) );
	CHECK_FALSE( library.addClip( 13, 2, "Orphan", slideClip( { 0.0f, 0.0f }, 1.0f, true, 0.0f ), -1.0f ) );
	CHECK( library.findClip( 1, "Lie" ) == 12 );
	CHECK( library.clips( 1 ).size() == 3 );
	REQUIRE( library.locomotion( 1 ) );

	auto slide = std::make_unique<SlideSystem>();
	SlideSystem& mover = *slide;
	world.addSystem( std::move( slide ) );
	world.addSystem( std::make_unique<AnimationSystem>() );
	entt::registry& registry = world.registry();
	const entt::entity entity = registry.create();
	registry.emplace<Transform>( entity );
	registry.emplace<PreviousTransform>( entity );
	AnimationState state;
	state.skeleton = 1;
	registry.emplace<AnimationState>( entity, state );

	// Идёт 1,5 м/с секунду: скорость догнала; фаза — цикл: пока сглаженная скорость ниже шага, клип на ходу — в своём темпе
	// (цикл в секунду), дошла — шаг за цикл даёт ту же скорость
	mover.speed = 1.5f;
	for( int i = 0; i < 60; ++i )
		world.tick( 1.0 / 60.0 );
	const AnimationState& walking = registry.get<AnimationState>( entity );
	CHECK( walking.current.speed > 1.4f );
	CHECK( walking.current.phase == doctest::Approx( 1.0 ).epsilon( 1e-4 ) );
	// Быстрее шага — тот же клип быстрее
	mover.speed = 3.0f;
	const double walked = walking.current.phase;
	for( int i = 0; i < 60; ++i )
		world.tick( 1.0 / 60.0 );
	CHECK( registry.get<AnimationState>( entity ).current.phase - walked > 1.7 );

	// Упёрлась: поведение просит идти, но места не меняется — скорость к нулю, клип покоя
	mover.speed = 0.0f;
	for( int i = 0; i < 60; ++i )
		world.tick( 1.0 / 60.0 );
	const AnimationState& stopped = registry.get<AnimationState>( entity );
	CHECK( stopped.current.speed < 0.01f );
	CHECK( library.locomotion( 1 )->evaluate( stopped.current.speed ).samples[0].clip == 10 );

	// Время игры стоит — шагов нет, фаза и время стоят
	const double phase = stopped.current.phase;
	world.clock().setGameTimeRunning( false );
	world.tick( 0.5 );
	CHECK( registry.get<AnimationState>( entity ).current.phase == phase );
}

TEST_CASE( "playAnimation: the new clip fades in from the frozen pose it replaces" )
{
	World world;
	AnimationLibrary& library = world.animations();
	library.addSkeleton( 1, "test", oneJointSkeleton() );
	REQUIRE( library.addClip( 10, 1, "Idle", slideClip( { 0.0f, 0.0f }, 1.0f, true, 0.0f ), 0.0f ) );
	REQUIRE( library.addClip( 12, 1, "Lie", slideClip( { 4.0f, 4.0f }, 1.0f, false, 0.0f ), -1.0f ) );
	world.addSystem( std::make_unique<AnimationSystem>() );
	entt::registry& registry = world.registry();
	const entt::entity entity = registry.create();
	AnimationState state;
	state.skeleton = 1;
	registry.emplace<AnimationState>( entity, state );

	PoseScratch scratch;
	std::vector<JointTransform> pose;
	playAnimation( registry.get<AnimationState>( entity ), clipPlayback( 12 ) );
	REQUIRE( evaluatePose( library, registry.get<AnimationState>( entity ), 1.0f, scratch, pose ) );
	CHECK( pose[0].translation.x == doctest::Approx( 0.0f ) );		// начало перехода — прежняя поза
	// 7 шагов по 1/60 с из 15 за переход
	for( int i = 0; i < 7; ++i )
		world.tick( 1.0 / 60.0 );
	evaluatePose( library, registry.get<AnimationState>( entity ), 1.0f, scratch, pose );
	CHECK( pose[0].translation.x == doctest::Approx( 4.0f * 7.0f / 15.0f ) );
	for( int i = 0; i < 10; ++i )
		world.tick( 1.0 / 60.0 );
	evaluatePose( library, registry.get<AnimationState>( entity ), 0.0f, scratch, pose );
	CHECK( pose[0].translation.x == doctest::Approx( 4.0f ) );
}

TEST_CASE( "Skeleton and AnimationClip: files as Tools/import_gltf.py writes them" )
{
	std::vector<uint8_t> skel;
	skel.insert( skel.end(), { 'D', 'M', 'S', 'K' } );
	put( skel, uint32_t( 1 ) );
	put( skel, uint32_t( 2 ) );
	const char* names[2] = { "RigRoot", "RigHead" };
	for( int i = 0; i < 2; ++i )
	{
		put( skel, uint32_t( std::strlen( names[i] ) ) );
		skel.insert( skel.end(), names[i], names[i] + std::strlen( names[i] ) );
		put( skel, int32_t( i - 1 ) );
		put( skel, joint( 0.0f, i ? 0.5f : 1.0f, 0.0f ) );
	}
	Skeleton skeleton;
	std::string reason;
	REQUIRE( skeleton.parse( skel.data(), skel.size(), reason ) );
	CHECK( skeleton.jointCount() == 2 );
	CHECK( skeleton.joint( 1 ).name == "RigHead" );
	CHECK( skeleton.joint( 1 ).parent == 0 );
	CHECK( skeleton.inverseBind()[1]._42 == doctest::Approx( -1.5f ) );
	CHECK_FALSE( skeleton.parse( skel.data(), skel.size() - 3, reason ) );

	std::vector<uint8_t> anim;
	anim.insert( anim.end(), { 'D', 'M', 'A', 'N' } );
	put( anim, uint32_t( 1 ) );
	put( anim, uint32_t( 2 ) );		// кости
	put( anim, uint32_t( 2 ) );		// кадры
	put( anim, 30.0f );
	put( anim, uint32_t( 1 ) );		// цикл
	put( anim, 1.68f );
	put( anim, 0.1f );
	put( anim, XMFLOAT3( -1.0f, 0.0f, -2.0f ) );
	put( anim, XMFLOAT3( 1.0f, 2.4f, 2.0f ) );
	for( int frame = 0; frame < 2; ++frame )
		for( int i = 0; i < 2; ++i )
			put( anim, joint( 0.0f, 1.0f, 0.1f * frame ) );
	for( int frame = 0; frame < 2; ++frame )
		put( anim, AnimationClip::RootMotion{ 0.0f, 0.056f * frame, 0.0f } );
	AnimationClip clip;
	REQUIRE( clip.parse( anim.data(), anim.size(), reason ) );
	CHECK( clip.jointCount() == 2 );
	CHECK( clip.duration() == doctest::Approx( 1.0f / 30.0f ) );
	CHECK( clip.looping() );
	CHECK( clip.rootSpeed() == doctest::Approx( 1.68f ) );
	CHECK( clip.boundsMax().y == doctest::Approx( 2.4f ) );
	CHECK( clip.rootMotion()[1].z == doctest::Approx( 0.056f ) );
	CHECK_FALSE( clip.parse( anim.data(), anim.size() - 4, reason ) );
}
