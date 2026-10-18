#include "SceneCommands.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>
#include "Scene.h"
#include "Terrain\TerrainPaintTool.h"
#include "Terrain\TerrainPipeline.h"
#include "Water\WaterSimulation.h"
#include "Engine\Simulation\Components.h"

namespace GS
{

namespace
{

bool number( const std::string& text, float& value )
{
	try
	{
		value = std::stof( text );
		return true;
	}
	catch( const std::exception& )
	{
		return false;
	}
}

// terrain paint: мазок кисти, clear, list, probe (TerrainPaintTool::stroke)
void paintCommand( Scene& scene, const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
{
	TerrainPipeline& terrain = scene.terrainPipeline();
	if( args.size() == 2 && args[1] == "list" )
	{
		std::string text;
		for( const auto& [target, tiles] : terrain.paint().targets() )
			text += target + ": " + std::to_string( tiles ) + " tiles\n";
		return reply->ok( text.empty() ? "no paint" : text );
	}
	if( args.size() == 3 && args[1] == "clear" )
		return terrain.clearPaint( args[2] ) ? reply->ok( "cleared " + args[2] ) : reply->error( "no paint of " + args[2] );
	float x = 0.0f, z = 0.0f;
	if( args.size() == 5 && args[1] == "probe" && number( args[3], x ) && number( args[4], z ) )
	{
		float rule = 0.0f, delta = 0.0f, result = 0.0f;
		if( !terrain.paintProbe( args[2], x, z, rule, delta, result ) )
			return reply->error( "no target " + args[2] + " at this point" );
		char text[128];
		std::snprintf( text, sizeof( text ), "rule %.3f, delta %.3f, result %.3f", rule, delta, result );
		return reply->ok( text );
	}
	static const std::pair<const char*, TerrainPaint::Mode> modes[] = { { "value", TerrainPaint::Mode::value }, { "step", TerrainPaint::Mode::step },
																		 { "unstep", TerrainPaint::Mode::unstep }, { "erase", TerrainPaint::Mode::erase } };
	float radius = 0.0f, value = 0.0f, falloff = 0.5f;
	const auto mode = args.size() >= 7 ? std::find_if( std::begin( modes ), std::end( modes ), [&]( const auto& m ) { return args[5] == m.first; } ) :
										 std::end( modes );
	if( args.size() < 7 || args.size() > 8 || !number( args[2], x ) || !number( args[3], z ) || !number( args[4], radius ) ||
		mode == std::end( modes ) || !number( args[6], value ) || ( args.size() == 8 && !number( args[7], falloff ) ) )
		return reply->error( "usage: terrain paint <target> <x> <z> <radius> value|step|unstep|erase <value> [falloff] | paint clear <target> | "
							 "paint list | paint probe <target> <x> <z>" );
	// Цель — первое слово после paint: «layer N» пишется в кавычках
	std::string reason;
	if( !scene.terrainPaint().stroke( args[1], mode->second, value, x, z, radius, falloff, reason ) )
		return reply->error( reason );
	reply->ok();
}

}

void registerSceneCommands( ConsoleCommands& console, Scene& scene )
{
	console.registerCommand( "world", "- entities, steps and clock of the simulation world; spawn <model> <x> <z> [count=5] "
							 "[radius=15] - wanderers around the point (a loaded model: Sphere, Cube, Elk...); list - places of the "
							 "entities (and clip, speed, phase of animated ones); clear - remove all entities; speed <m/s> - wander "
							 "speed of all entities; anim <clip>|locomotion [<clip>|locomotion ...] - all animated entities play the "
							 "clip of their skeleton, the next ones when a one-shot clip ends, "
							 "locomotion - back to the blend by speed; anim - clips of the skeletons",
							 [&scene]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		World& world = scene.world();
		if( args.empty() )
		{
			const WorldClock& clock = world.clock();
			const WorldClock::Date date = clock.date();
			char text[384];
			std::snprintf( text, sizeof( text ), "entities %zu, steps %llu (last frame %u), step rate %.0f Hz%s; clock %04d-%02d-%02d "
						   "%.3f h, time scale %g%s; game time %.2f s%s; physics: %zu bodies, %zu characters%s", world.entityCount(),
						   static_cast<unsigned long long>( world.steps() ), world.lastSteps(), world.stepRate(),
						   world.paused() ? ", paused" : "", date.year, date.month, date.day, clock.hours(), clock.timeScale(),
						   clock.paused() ? ", paused" : "", clock.gameSeconds(), clock.gameTimeRunning() ? "" : " (frozen)",
						   world.physics().bodyCount(), world.physics().characterCount(),
						   world.physics().terrainRevision() != ~0u ? ", terrain" : ", no terrain" );
			return reply->ok( text );
		}
		if( args.size() == 1 && args[0] == "list" )
		{
			std::string text;
			const AnimationLibrary& animations = world.animations();
			for( auto [entity, transform] : world.registry().view<const Transform>().each() )
			{
				char line[192];
				int length = std::snprintf( line, sizeof( line ), "%u: %.2f,%.2f,%.2f", static_cast<uint32_t>( entity ),
											transform.position.x, transform.position.y, transform.position.z );
				if( const AnimationState* state = world.registry().try_get<AnimationState>( entity ) )
				{
					const AnimationPlayback& playback = state->current;
					if( playback.mode == AnimationPlayback::Mode::clip )
						std::snprintf( line + length, sizeof( line ) - length, " clip %s %.2f s",
									   animations.clipName( playback.clip ).c_str(), playback.time );
					else
						std::snprintf( line + length, sizeof( line ) - length, " locomotion %.2f m/s turn %.0f deg/s phase %.2f",
									   playback.speed, playback.turn * 57.29578f, playback.phase - std::floor( playback.phase ) );
					length = static_cast<int>( std::strlen( line ) );
					std::snprintf( line + length, sizeof( line ) - length, " heading %.0f deg", headingOf( transform.rotation ) * 57.29578f );
				}
				text += line;
				text += '\n';
			}
			return reply->ok( text.empty() ? "no entities" : text );
		}
		if( args.size() == 1 && args[0] == "clear" )
		{
			world.clear();
			return reply->ok( "entities removed" );
		}
		if( args.size() == 2 && args[0] == "speed" )
		{
			float speed = 0.0f;
			if( !number( args[1], speed ) || speed < 0.0f )
				return reply->error( "usage: world speed <m/s>" );
			return reply->ok( "wander speed " + args[1] + " m/s: " + std::to_string( scene.worldEntities().setWanderSpeed( speed ) ) +
							  " entities" );
		}
		if( args.size() == 1 && args[0] == "anim" )
		{
			// Клипы скелетов сущностей: имя и длительность, точки шага по скорости
			const AnimationLibrary& animations = world.animations();
			std::set<uint32_t> skeletons;
			for( auto [entity, state] : world.registry().view<const AnimationState>().each() )
				skeletons.insert( state.skeleton );
			std::string text;
			for( uint32_t skeleton : skeletons )
			{
				text += animations.skeletonName( skeleton ) + ":";
				for( uint32_t clip : animations.clips( skeleton ) )
				{
					char item[96];
					std::snprintf( item, sizeof( item ), " %s %.2fs", animations.clipName( clip ).c_str(),
								   animations.clip( clip )->duration() );
					text += item;
				}
				text += '\n';
			}
			return reply->ok( text.empty() ? "no animated entities" : text );
		}
		if( args.size() >= 2 && args[0] == "anim" )
		{
			// Цепочка: клип, затем следующие, когда разовый дойдёт до конца; locomotion — шаг по скорости
			std::vector<std::string> clips;
			std::string text;
			for( size_t i = 1; i < args.size(); ++i )
			{
				clips.push_back( args[i] == "locomotion" ? std::string() : args[i] );
				text += ( i > 1 ? " -> " : "" ) + args[i];
			}
			const size_t count = scene.worldEntities().playClips( clips );
			if( count == 0 && !( clips.size() == 1 && clips[0].empty() ) )
				return reply->error( "no animated entity has all of the clips " + text + " (world anim - the clips)" );
			return reply->ok( text + ": " + std::to_string( count ) + " entities" );
		}
		float x = 0.0f, z = 0.0f, count = 5.0f, radius = 15.0f;
		if( args[0] != "spawn" || args.size() < 4 || args.size() > 6 || !number( args[2], x ) || !number( args[3], z ) ||
			( args.size() > 4 && !number( args[4], count ) ) || ( args.size() > 5 && !number( args[5], radius ) ) || count < 1.0f ||
			radius < 1.0f )
			return reply->error( "usage: world | world spawn <model> <x> <z> [count] [radius] | world list | world clear | "
								 "world speed <m/s> | world anim [<clip>|locomotion ...]" );
		std::string reason;
		if( !scene.worldEntities().spawnWanderers( args[1], x, z, static_cast<uint32_t>( count ), radius, reason ) )
			return reply->error( reason );
		reply->ok( "spawned " + std::to_string( static_cast<uint32_t>( count ) ) + ", entities " + std::to_string( world.entityCount() ) );
	} );
	console.registerCommand( "water", "discharge <file.dds> - water discharge of the cells, m3/s, as R32_FLOAT DDS; "
							 "state <prefix> - water for the shaders as R32_FLOAT DDS: <prefix>_depth.dds (m), <prefix>_vx.dds and "
							 "<prefix>_vz.dds (flow along world X and Z, m/s)",
							 [&scene]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.size() != 2 || ( args[0] != "discharge" && args[0] != "state" ) )
		{
			reply->error( "usage: water discharge <file.dds> | water state <prefix>" );
			return;
		}
		std::string reason;
		const bool written = args[0] == "discharge" ? scene.water().exportDischarge( args[1], reason ) :
													 scene.water().exportState( args[1], reason );
		if( written )
			reply->ok( "written " + args[1] );
		else
			reply->error( reason );
	} );
	console.registerCommand( "terrain", "paint <target> <x> <z> <radius> value|step|unstep|erase <value> [falloff] - a brush dab over "
							 "the layer rules at a world point (target: \"layer N\" or a mask name); paint clear <target>; paint list; "
							 "paint probe <target> <x> <z> - rule, paint delta and result; "
							 "hydrology <prefix> - water channels of the level as R32_FLOAT DDS: <prefix>_lowering.dds "
							 "(bed lowering, m), <prefix>_level.dds (stream water level, m), <prefix>_lake.dds (lake level, m; -1e9 - no lake), <prefix>_detail.dds (detail ground near channels, m, "
							 "4 samples per height map texel; -1e9 - no detail tile)",
							 [&scene]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( !args.empty() && args[0] == "paint" )
			return paintCommand( scene, args, reply );
		if( args.size() != 2 || args[0] != "hydrology" )
			return reply->error( "usage: terrain hydrology <prefix> | terrain paint ..." );
		std::string reason;
		if( !scene.terrainPipeline().exportHydrology( args[1], reason ) )
			return reply->error( "terrain hydrology: " + reason );
		reply->ok( "written " + args[1] + "_lowering.dds, _level.dds, _lake.dds" );
	} );
}

}
