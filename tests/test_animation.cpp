// Скелетная анимация (ядро): выборка клипа, поза в пространстве модели и матрицы скиннинга, смешение мест костей, шаг по
// скорости и повороту (LocomotionBlendSpace: ряды аллюров, варианты с поворотом), движение корня клипа за отрезок, система
// анимации сущностей (скорость и поворот, переход, очередь клипов), передвижение движением корня, чтение файлов .skel и .anim
#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "Engine\Animation\Skeleton.h"
#include "Engine\Animation\AnimationClip.h"
#include "Engine\Animation\AnimationPose.h"
#include "Engine\Animation\LocomotionBlendSpace.h"
#include "Engine\Animation\AnimationLibrary.h"
#include "Engine\Animation\AnimationSystem.h"
#include "Engine\Simulation\World.h"
#include "Engine\Simulation\Components.h"
#include "Engine\Simulation\MovementSystem.h"

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

TEST_CASE( "LocomotionBlendSpace: weights by speed, the phase rate keeps the blended stride at the speed" )
{
	LocomotionBlendSpace blend;
	blend.addSample( 3, 6.0f, 0.6f );		// рысь: шаг 3,6 м за цикл
	blend.addSample( 1, 0.0f, 5.0f );		// покой
	blend.addSample( 2, 1.5f, 1.0f );		// шаг: 1,5 м за цикл
	REQUIRE( blend.rows().size() == 3 );
	CHECK( blend.rows().front().straight.clip == 1 );

	LocomotionBlendSpace::Result still = blend.evaluate( 0.0f );
	CHECK( still.count == 1 );
	CHECK( still.samples[0].clip == 1 );
	CHECK_FALSE( still.samples[0].moving );
	CHECK( still.phaseRate == 0.0f );

	// Между покоем и шагом: клип на ходу — в своём темпе, цикл в секунду
	LocomotionBlendSpace::Result slow = blend.evaluate( 0.75f );
	REQUIRE( slow.count == 2 );
	CHECK( slow.samples[0].weight == doctest::Approx( 0.5f ) );
	CHECK( slow.samples[1].clip == 2 );
	CHECK( slow.phaseRate == doctest::Approx( 1.0f ) );

	// Между шагом и рысью: смешанный шаг за цикл × циклов в секунду = скорость
	LocomotionBlendSpace::Result mixed = blend.evaluate( 3.75f );
	REQUIRE( mixed.count == 2 );
	CHECK( mixed.samples[1].weight == doctest::Approx( 0.5f ) );
	CHECK( mixed.phaseRate * ( 0.5f * 1.5f + 0.5f * 3.6f ) == doctest::Approx( 3.75f ) );

	// Выше последней точки — рысь быстрее
	LocomotionBlendSpace::Result fast = blend.evaluate( 9.0f );
	CHECK( fast.count == 1 );
	CHECK( fast.samples[0].clip == 3 );
	CHECK( fast.phaseRate == doctest::Approx( 2.5f ) );
}

TEST_CASE( "LocomotionBlendSpace: left and right variants of a gait by the turn" )
{
	LocomotionBlendSpace blend;
	blend.addSample( 21, 1.5f, 1.0f, -1.0f );	// шаг влево — раньше своего ряда
	blend.addSample( 1, 0.0f, 5.0f );			// покой
	blend.addSample( 2, 1.5f, 1.0f );			// шаг
	blend.addSample( 22, 1.5f, 1.0f, 1.0f );	// шаг вправо
	blend.addSample( 3, 6.0f, 0.6f );			// рысь
	blend.addSample( 31, 6.0f, 0.6f, -2.5f );	// рысь влево; вправо варианта нет
	REQUIRE( blend.rows().size() == 3 );
	CHECK( blend.rows()[1].hasLeft );
	CHECK( blend.rows()[1].hasRight );
	CHECK( blend.rows()[2].hasLeft );
	CHECK_FALSE( blend.rows()[2].hasRight );
	CHECK_FALSE( blend.rows()[0].hasLeft );

	auto weightOf = []( const LocomotionBlendSpace::Result& result, uint32_t clip )
	{
		float weight = 0.0f;
		for( uint32_t i = 0; i < result.count; ++i )
			if( result.samples[i].clip == clip )
				weight += result.samples[i].weight;
		return weight;
	};
	// Прямо — только прямой клип; половина поворота клипа — пополам; больше — только вариант
	CHECK( weightOf( blend.evaluate( 1.5f, 0.0f ), 2 ) == doctest::Approx( 1.0f ) );
	const LocomotionBlendSpace::Result half = blend.evaluate( 1.5f, -0.5f );
	CHECK( weightOf( half, 2 ) == doctest::Approx( 0.5f ) );
	CHECK( weightOf( half, 21 ) == doctest::Approx( 0.5f ) );
	const LocomotionBlendSpace::Result sharp = blend.evaluate( 1.5f, -2.0f );
	CHECK( sharp.count == 1 );
	CHECK( sharp.samples[0].clip == 21 );
	CHECK( weightOf( blend.evaluate( 1.5f, 0.7f ), 22 ) == doctest::Approx( 0.7f ) );
	// Вправо у рыси варианта нет — прямо
	CHECK( weightOf( blend.evaluate( 6.0f, 1.0f ), 3 ) == doctest::Approx( 1.0f ) );

	// Между шагом и рысью с поворотом влево: до 4 клипов, веса в сумме 1, смешанный шаг держит скорость
	const LocomotionBlendSpace::Result four = blend.evaluate( 3.75f, -0.5f );
	REQUIRE( four.count == 4 );
	CHECK( weightOf( four, 2 ) == doctest::Approx( 0.25f ) );
	CHECK( weightOf( four, 21 ) == doctest::Approx( 0.25f ) );
	CHECK( weightOf( four, 3 ) == doctest::Approx( 0.4f ) );
	CHECK( weightOf( four, 31 ) == doctest::Approx( 0.1f ) );
	CHECK( four.phaseRate * ( 0.5f * 1.5f + 0.5f * 3.6f ) == doctest::Approx( 3.75f ) );
}

TEST_CASE( "AnimationClip: root motion between two moments is in the axes of the root at the first one" )
{
	// Секунду — метр вперёд, следующую — метр вправо с поворотом на 90° вправо (кривая — в осях первого кадра)
	auto clip = slideClip( { 0.0f, 0.0f, 0.0f }, 1.0f, false, 1.0f );
	clip->setRootMotionCurve( { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, pi / 2.0f } } );
	AnimationClip::RootMotion first = clip->rootMotionDelta( 0.0, 1.0 );
	CHECK( first.x == doctest::Approx( 0.0f ) );
	CHECK( first.z == doctest::Approx( 1.0f ) );
	AnimationClip::RootMotion second = clip->rootMotionDelta( 1.0, 2.0 );
	CHECK( second.x == doctest::Approx( 1.0f ) );
	CHECK( second.z == doctest::Approx( 0.0f ).epsilon( 1e-5 ) );
	CHECK( second.yaw == doctest::Approx( pi / 2.0f ) );
	// Разовый — в пределах клипа
	CHECK( clip->rootMotionDelta( 2.0, 5.0 ).z == doctest::Approx( 0.0f ) );

	// Зацикленный — через стыки: метр вперёд за цикл, с 0,5 до 2,5 — два метра
	auto loop = slideClip( { 0.0f, 0.0f }, 1.0f, true, 1.0f );
	loop->setRootMotionCurve( { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } } );
	const AnimationClip::RootMotion two = loop->rootMotionDelta( 0.5, 2.5 );
	CHECK( two.z == doctest::Approx( 2.0f ) );
	CHECK( two.x == doctest::Approx( 0.0f ) );
	CHECK( two.yaw == doctest::Approx( 0.0f ) );
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

TEST_CASE( "AnimationSystem: a queued clip follows one pass of the clip before it, the last looping one stays" )
{
	World world;
	AnimationLibrary& library = world.animations();
	library.addSkeleton( 1, "test", oneJointSkeleton() );
	REQUIRE( library.addClip( 10, 1, "Idle", slideClip( { 0.0f, 0.0f }, 1.0f, true, 0.0f ), 0.0f ) );
	// 0,1 с; зацикленный, как разворот на месте у импорта (поза в конце та же) — перед следующим играет один проход
	REQUIRE( library.addClip( 12, 1, "Lie", slideClip( { 4.0f, 4.0f }, 10.0f, true, 0.0f ), -1.0f ) );
	REQUIRE( library.addClip( 13, 1, "Lying", slideClip( { 5.0f, 5.0f }, 1.0f, true, 0.0f ), -1.0f ) );
	world.addSystem( std::make_unique<AnimationSystem>() );
	entt::registry& registry = world.registry();
	const entt::entity entity = registry.create();
	AnimationState state;
	state.skeleton = 1;
	registry.emplace<AnimationState>( entity, state );

	const AnimationPlayback lieDown[] = { clipPlayback( 12 ), clipPlayback( 13 ) };
	playSequence( registry.get<AnimationState>( entity ), lieDown, 2 );
	CHECK( registry.get<AnimationState>( entity ).queued == 1 );
	for( int i = 0; i < 12; ++i )
		world.tick( 1.0 / 60.0 );
	CHECK( registry.get<AnimationState>( entity ).current.clip == 13 );
	CHECK( registry.get<AnimationState>( entity ).queued == 0 );
	for( int i = 0; i < 120; ++i )
		world.tick( 1.0 / 60.0 );
	CHECK( registry.get<AnimationState>( entity ).current.clip == 13 );

	// Разовый, затем шаг по скорости
	const AnimationPlayback standUp[] = { clipPlayback( 12 ), AnimationPlayback() };
	playSequence( registry.get<AnimationState>( entity ), standUp, 2 );
	for( int i = 0; i < 12; ++i )
		world.tick( 1.0 / 60.0 );
	CHECK( registry.get<AnimationState>( entity ).current.mode == AnimationPlayback::Mode::locomotion );
}

namespace
{

// Поворачивает сущности вокруг вертикали со скоростью rate, рад/с (до системы анимации, как MovementSystem)
class SpinSystem : public SimulationSystem
{
public:
	const char* name() const override { return "Spin"; }
	void step( World& world, float seconds ) override
	{
		for( auto [entity, transform] : world.registry().view<Transform>().each() )
			XMStoreFloat4( &transform.rotation, XMQuaternionRotationRollPitchYaw( 0.0f, headingOf( transform.rotation ) +
																					rate * seconds, 0.0f ) );
	}
	float rate = 0.0f;
};

}

TEST_CASE( "Animation LOD: the update step doubles every LOD distance up to the limit, entities take turns" )
{
	CHECK( animationUpdateStep( 10.0f, 40.0f, 4 ) == 1 );
	CHECK( animationUpdateStep( 45.0f, 40.0f, 4 ) == 2 );
	CHECK( animationUpdateStep( 85.0f, 40.0f, 4 ) == 4 );
	CHECK( animationUpdateStep( 500.0f, 40.0f, 4 ) == 4 );
	CHECK( animationUpdateStep( 500.0f, 40.0f, 3 ) == 3 );
	CHECK( animationUpdateStep( 500.0f, 40.0f, 1 ) == 1 );		// без LOD
	CHECK( animationUpdateStep( 500.0f, 0.0f, 8 ) == 1 );

	// За шаг кадров каждая сущность обновляется ровно раз, в кадре — четверть из них
	constexpr uint32_t step = 4, entities = 40;
	uint32_t updates[entities] = {};
	for( uint64_t frame = 100; frame < 100 + step; ++frame )
	{
		uint32_t inFrame = 0;
		for( uint32_t serial = 0; serial < entities; ++serial )
			if( updatesThisFrame( frame, serial, step ) )
			{
				++updates[serial];
				++inFrame;
			}
		CHECK( inFrame == entities / step );
	}
	for( uint32_t count : updates )
		CHECK( count == 1 );
}

TEST_CASE( "AnimationSystem: the turn of an entity is measured from its heading" )
{
	World world;
	world.animations().addSkeleton( 1, "test", oneJointSkeleton() );
	auto spin = std::make_unique<SpinSystem>();
	spin->rate = 1.0f;
	world.addSystem( std::move( spin ) );
	world.addSystem( std::make_unique<AnimationSystem>() );
	entt::registry& registry = world.registry();
	const entt::entity entity = registry.create();
	registry.emplace<Transform>( entity );
	registry.emplace<PreviousTransform>( entity );
	AnimationState state;
	state.skeleton = 1;
	registry.emplace<AnimationState>( entity, state );
	for( int i = 0; i < 120; ++i )
		world.tick( 1.0 / 60.0 );
	CHECK( registry.get<AnimationState>( entity ).current.turn == doctest::Approx( 1.0f ).epsilon( 0.01 ) );
}

TEST_CASE( "MovementSystem: an action clip moves and turns the entity by its root motion" )
{
	World world;
	AnimationLibrary& library = world.animations();
	library.addSkeleton( 1, "test", oneJointSkeleton() );
	// Секунду — 2 м вперёд, следующую — разворот на месте на 90° вправо
	auto clip = slideClip( { 0.0f, 0.0f, 0.0f }, 1.0f, false, 1.0f );
	clip->setRootMotionCurve( { { 0.0f, 0.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, { 0.0f, 2.0f, pi / 2.0f } } );
	REQUIRE( library.addClip( 14, 1, "WalkAndTurn", clip, -1.0f ) );
	REQUIRE( library.addClip( 15, 1, "Lie", slideClip( { 0.0f, 0.0f }, 1.0f, false, 0.0f ), -1.0f ) );
	world.addSystem( std::make_unique<MovementSystem>() );
	world.addSystem( std::make_unique<AnimationSystem>() );
	entt::registry& registry = world.registry();
	const entt::entity entity = registry.create();
	Transform transform;
	transform.position = XMFLOAT3( 10.0f, 0.0f, 10.0f );
	XMStoreFloat4( &transform.rotation, XMQuaternionRotationRollPitchYaw( 0.0f, pi / 2.0f, 0.0f ) );	// взгляд — +X
	registry.emplace<Transform>( entity, transform );
	registry.emplace<PreviousTransform>( entity, PreviousTransform{ transform } );
	// Поведение просит идти к −Z — во время действия не действует
	registry.emplace<Locomotion>( entity, Locomotion{ XMFLOAT2( 0.0f, -3.0f ), pi } );
	AnimationState state;
	state.skeleton = 1;
	registry.emplace<AnimationState>( entity, state );
	playAnimation( registry.get<AnimationState>( entity ), clipPlayback( 14 ) );

	for( int i = 0; i < 150; ++i )
		world.tick( 1.0 / 60.0 );
	const Transform& moved = registry.get<Transform>( entity );
	CHECK( moved.position.x == doctest::Approx( 12.0f ).epsilon( 0.01 ) );		// 2 м по взгляду (+X)
	CHECK( moved.position.z == doctest::Approx( 10.0f ).epsilon( 0.01 ) );
	CHECK( std::cos( headingOf( moved.rotation ) ) == doctest::Approx( -1.0f ).epsilon( 0.001 ) );	// +X → −Z: ещё 90° вправо

	// Клип без движения корня — стоит
	playAnimation( registry.get<AnimationState>( entity ), clipPlayback( 15 ) );
	const XMFLOAT3 before = registry.get<Transform>( entity ).position;
	for( int i = 0; i < 60; ++i )
		world.tick( 1.0 / 60.0 );
	CHECK( registry.get<Transform>( entity ).position.x == before.x );
	CHECK( registry.get<Transform>( entity ).position.z == before.z );
}
