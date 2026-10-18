#include "WorldEntities.h"
#include <algorithm>
#include <cmath>
#include "System.h"
#include "RenderView.h"
#include "Model\ModelDraw.h"
#include "Terrain\TerrainHeightSource.h"
#include "Engine\Simulation\Components.h"
#include "Engine\Simulation\WanderSystem.h"
#include "Engine\Animation\AnimationLibrary.h"
#include "Engine\Animation\AnimationPose.h"

using namespace DirectX;

namespace GS
{

WorldEntities::WorldEntities() :
	SceneObject( "World" )
{
	m_properties.setName( "World" );
	Property* rate = m_properties.insert( "Step rate", World::defaultStepRate );
	rate->setLow( 10.0f );
	rate->setHigh( 240.0f );
	rate->setUnit( "Hz" )->setTooltip( "Fixed steps of the simulation per second; frames interpolate between the steps" );
	m_properties.insert( "Simulation paused", false )->setTooltip( "Entities stand still; the world clock goes on" );
	m_properties.insert( "Show collision", false )->setTooltip( "Collision shapes of bodies and characters within 30 m of the camera, "
																  "as lines over the frame (View > Debug views > Collision)" );
	m_properties.insert( "Show skeleton", false )->setTooltip( "Bones of animated models (entities and level instances) as lines "
																 "over the frame (View > Debug views > Skeleton)" );
	// LOD анимации (как Update Rate Optimizations в UE): дальше — поза реже
	Property* lodDistance = m_properties.insert( "Animation LOD distance", 40.0f );
	lodDistance->setLow( 5.0f );
	lodDistance->setHigh( 500.0f );
	lodDistance->setUnit( "m" )->setTooltip( "Every this distance from the camera the pose of an animated entity is updated half "
											  "as often (up to Animation LOD max skip); invisible entities get no pose" );
	Property* maxSkip = m_properties.insert( "Animation LOD max skip", static_cast<int32_t>( 4 ) );
	maxSkip->setLow( 1 );
	maxSkip->setHigh( 16 );
	maxSkip->setTooltip( "Far entities update the pose at most every this many frames; 1 - every frame (no LOD)" );
}

void WorldEntities::initialize( World& world )
{
	m_world = &world;
}

void WorldEntities::update( const FrameContext& frame )
{
	if( !m_world )
		return;
	m_world->setStepRate( m_properties["Step rate"].data<float>() );
	const bool paused = m_properties["Simulation paused"].data<bool>();
	if( paused != m_world->paused() )
		m_world->setPaused( paused );
}

XMMATRIX WorldEntities::placeMatrix( entt::entity entity, const Transform& current, const ModelVisual& visual, float t,
									 XMFLOAT3& origin ) const
{
	const PreviousTransform* previous = m_world->registry().try_get<PreviousTransform>( entity );
	const Transform place = previous ? interpolate( previous->transform, current, t ) : current;
	// Точка места — на земле, низ модели — на ней
	origin = XMFLOAT3( place.position.x, place.position.y + visual.groundOffset * place.scale, place.position.z );
	return XMMatrixScaling( place.scale, place.scale, place.scale ) * XMMatrixRotationQuaternion( XMLoadFloat4( &place.rotation ) ) *
		   XMMatrixTranslation( origin.x, origin.y, origin.z );
}

namespace
{

// Задевает ли модель (границы во всех позах клипов, мировой матрицей) хоть один вид кадра — главный или каскад теней; съёмку
// сцены сущности не рисуют
bool visibleInFrame( const BoundingBox& local, FXMMATRIX world, const FrameContext& frame )
{
	BoundingBox bounds;
	local.Transform( bounds, world );
	const XMFLOAT3 low( bounds.Center.x - bounds.Extents.x, bounds.Center.y - bounds.Extents.y, bounds.Center.z - bounds.Extents.z );
	const XMFLOAT3 high( bounds.Center.x + bounds.Extents.x, bounds.Center.y + bounds.Extents.y, bounds.Center.z + bounds.Extents.z );
	if( frame.viewCount == 0 )
		return frame.view.frustum.checkBox( low, high );
	for( uint32_t i = 0; i < frame.viewCount; ++i )
		if( frame.views[i] && frame.views[i]->kind != RenderView::Kind::capture && frame.views[i]->frustum.checkBox( low, high ) )
			return true;
	return false;
}

}

void WorldEntities::compute( const FrameContext& frame )
{
	m_skinning.clear();
	m_boneOffsets.clear();
	m_boneLines.clear();
	m_posed = 0;
	m_cachedPoses = 0;
	if( !m_world )
		return;
	++m_frame;
	const AnimationLibrary& library = m_world->animations();
	const float t = m_world->interpolation();
	const bool showSkeleton = m_properties["Show skeleton"].data<bool>();
	const float lodDistance = m_properties["Animation LOD distance"].data<float>();
	const uint32_t maxSkip = static_cast<uint32_t>( std::max( m_properties["Animation LOD max skip"].data<int32_t>(), 1 ) );
	const XMVECTOR camera = XMLoadFloat3( &frame.view.position );
	const entt::registry& registry = m_world->registry();
	for( auto [entity, current, visual, state] : registry.view<const Transform, const ModelVisual, const AnimationState>().each() )
	{
		const Skeleton* skeleton = library.skeleton( state.skeleton );
		const DMModel* model = System::models().get( visual.model ).get();
		if( !skeleton || !model )
			continue;
		XMFLOAT3 origin;
		const XMMATRIX world = placeMatrix( entity, current, visual, t, origin );
		// Невидимая ни в одном виде кадра — без позы: отрисовка её так же отсекает
		if( !visibleInFrame( model->animatedBounds(), world, frame ) )
			continue;
		// LOD анимации: далёкая — поза реже, в остальных кадрах — прежняя из кэша (место смешивается каждый кадр). Не было
		// позы в прошлом кадре (была невидима) — заново
		PoseCache& cache = m_poseCache[entity];
		const float distance = XMVectorGetX( XMVector3Length( XMLoadFloat3( &origin ) - camera ) );
		const uint32_t step = animationUpdateStep( distance, lodDistance, maxSkip );
		if( cache.matrices.size() != skeleton->jointCount() || cache.frame + 1 != m_frame ||
			updatesThisFrame( m_frame, static_cast<uint32_t>( entt::to_integral( entity ) ), step ) )
		{
			if( !evaluatePose( library, state, t, m_poseScratch, m_pose ) )
				continue;
			modelSpace( *skeleton, m_pose, cache.joints );
			cache.matrices.resize( skeleton->jointCount() );
			skinningMatrices( *skeleton, cache.joints, cache.matrices.data() );
			++m_posed;
		}
		else
			++m_cachedPoses;
		cache.frame = m_frame;
		uint32_t offset = 0;
		std::copy( cache.matrices.begin(), cache.matrices.end(), m_skinning.append( cache.matrices.size(), offset ) );
		m_boneOffsets[entity] = offset;
		if( showSkeleton )
			appendBoneLines( *skeleton, cache.joints, world, m_boneLines );
	}
	// Позы убранных сущностей — прочь
	for( auto it = m_poseCache.begin(); it != m_poseCache.end(); )
		it = registry.valid( it->first ) ? std::next( it ) : m_poseCache.erase( it );
	m_skinning.upload();
}

void WorldEntities::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	// Съёмка сцены (карты отражений места) — без движущегося
	if( !m_world || view.kind == RenderView::Kind::capture )
		return;
	const float t = m_world->interpolation();
	const entt::registry& registry = m_world->registry();
	for( auto [entity, current, visual] : registry.view<const Transform, const ModelVisual>().each() )
	{
		const DMModel* model = System::models().get( visual.model ).get();
		if( !model || model->lodCount() == 0 )
			continue;
		XMFLOAT3 origin;
		const XMMATRIX world = placeMatrix( entity, current, visual, t, origin );
		auto posed = m_boneOffsets.find( entity );
		const SkinBinding skin{ &m_skinning, posed != m_boneOffsets.end() ? posed->second : 0 };
		collectModelMeshes( *model, visual.model, world, origin, view, collector, posed != m_boneOffsets.end() ? &skin : nullptr );
	}
}

size_t WorldEntities::setWanderSpeed( float speed )
{
	if( !m_world )
		return 0;
	size_t count = 0;
	for( auto [entity, wander] : m_world->registry().view<Wander>().each() )
	{
		wander.speed = std::max( speed, 0.0f );
		++count;
	}
	return count;
}

size_t WorldEntities::playClips( const std::vector<std::string>& clips )
{
	if( !m_world || clips.empty() )
		return 0;
	const AnimationLibrary& library = m_world->animations();
	size_t count = 0;
	for( auto [entity, state] : m_world->registry().view<AnimationState>().each() )
	{
		// Цепочка этого скелета: пустое имя — шаг по скорости; нет такого клипа у скелета — сущность не трогается
		AnimationPlayback sequence[1 + AnimationState::maxQueue];
		size_t length = 0;
		bool found = true;
		for( const std::string& clip : clips )
		{
			if( length == std::size( sequence ) )
				break;
			if( clip.empty() )
			{
				sequence[length++] = AnimationPlayback();
				continue;
			}
			const uint32_t id = library.findClip( state.skeleton, clip );
			found = found && id != 0;
			sequence[length++] = clipPlayback( id );
		}
		if( !found )
			continue;
		// Уже идёт шагом по скорости — снова не начинать (без лишнего перехода)
		if( length == 1 && clips[0].empty() && state.current.mode == AnimationPlayback::Mode::locomotion )
		{
			state.queued = 0;
			continue;
		}
		playSequence( state, sequence, length );
		++count;
	}
	return count;
}

bool WorldEntities::spawnWanderers( const std::string& model, float x, float z, uint32_t count, float radius, std::string& error )
{
	if( !m_world )
	{
		error = "the world is not initialized";
		return false;
	}
	if( !System::models().exists( model ) )
	{
		error = "model " + model + " is not loaded (models of the level, its sky and scatter sets)";
		return false;
	}
	const uint32_t modelId = System::models().id( model );
	DMModel* asset = System::models().get( modelId ).get();
	const DMModel::LodBlock* lod = asset ? asset->getLodById( 0 ) : nullptr;
	if( !lod )
	{
		error = "model " + model + " has no LOD";
		return false;
	}

	entt::registry& registry = m_world->registry();
	const TerrainHeightSource* terrain = m_world->terrain();
	PhysicsWorld& physics = m_world->physics();
	for( uint32_t i = 0; i < count; ++i )
	{
		// Место в круге — по золотому углу (ровно, без случайных кучек), направление и зерно — своё у каждой
		const uint32_t serial = ++m_spawned;
		const float angle = 2.39996323f * serial;
		const float distance = radius * 0.7f * std::sqrt( ( i + 0.5f ) / count );
		Transform transform;
		transform.position = XMFLOAT3( x + std::sin( angle ) * distance, 0.0f, z + std::cos( angle ) * distance );
		float ground;
		if( terrain && terrain->surfaceHeight( transform.position.x, transform.position.z, ground ) )
			transform.position.y = ground;

		Wander wander;
		wander.center = XMFLOAT2( x, z );
		wander.radius = radius;
		wander.heading = angle;
		wander.random = 0x9E3779B9u * serial;
		const entt::entity entity = registry.create();
		registry.emplace<Transform>( entity, transform );
		registry.emplace<PreviousTransform>( entity, PreviousTransform{ transform } );
		// Низ границ LOD 0 — на точку места
		registry.emplace<ModelVisual>( entity, ModelVisual{ modelId, -( lod->bounds.Center.y - lod->bounds.Extents.y ) } );
		registry.emplace<Wander>( entity, wander );
		registry.emplace<Locomotion>( entity, Locomotion{ XMFLOAT2( 0.0f, 0.0f ), angle } );
		// Модель со скелетом — анимация: шаг по скорости (клипы шага скелета), каждый — со своей фазы покоя
		if( asset->skeleton() != 0 && m_world->animations().skeleton( asset->skeleton() ) )
		{
			AnimationState animation;
			animation.skeleton = asset->skeleton();
			animation.current.time = 0.37 * serial;
			animation.previous = animation.current;
			registry.emplace<AnimationState>( entity, animation );
		}
		// Есть земля столкновений — персонаж: капсула высотой модели; толщина — радиус простой коллизии капсулой
		// (Models.collision_radius: у зверя — по ширине тела, иначе длинное тело застревает между стволами) или по границам
		// LOD 0 по горизонтали
		if( physics.terrainRevision() != ~0u )
		{
			PhysicsWorld::CharacterSettings settings;
			settings.radius = asset->collision() == DMModel::Collision::capsule && asset->collisionRadius() > 0.0f ?
							  asset->collisionRadius() :
							  std::max( std::max( lod->bounds.Extents.x, lod->bounds.Extents.z ), 0.1f );
			settings.height = std::max( 2.0f * lod->bounds.Extents.y, 2.0f * settings.radius + 0.05f );
			settings.stepHeight = std::min( 0.3f, 0.5f * settings.height );
			settings.maxWadeDepth = std::max( 0.5f * settings.height, 0.1f );
			registry.emplace<Character>( entity, Character{ physics.createCharacter( settings, transform.position ) } );
		}
	}
	return true;
}

}
