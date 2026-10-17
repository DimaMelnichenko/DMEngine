#include "SceneCommands.h"
#include <algorithm>
#include <cstdio>
#include <iterator>
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
							 "[radius=15] - wanderers around the point (a loaded model: Sphere, Cube...); list - places of the entities; "
							 "clear - remove all entities",
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
			for( auto [entity, transform] : world.registry().view<const Transform>().each() )
			{
				char line[96];
				std::snprintf( line, sizeof( line ), "%u: %.2f,%.2f,%.2f\n", static_cast<uint32_t>( entity ), transform.position.x,
							   transform.position.y, transform.position.z );
				text += line;
			}
			return reply->ok( text.empty() ? "no entities" : text );
		}
		if( args.size() == 1 && args[0] == "clear" )
		{
			world.clear();
			return reply->ok( "entities removed" );
		}
		float x = 0.0f, z = 0.0f, count = 5.0f, radius = 15.0f;
		if( args[0] != "spawn" || args.size() < 4 || args.size() > 6 || !number( args[2], x ) || !number( args[3], z ) ||
			( args.size() > 4 && !number( args[4], count ) ) || ( args.size() > 5 && !number( args[5], radius ) ) || count < 1.0f ||
			radius < 1.0f )
			return reply->error( "usage: world | world spawn <model> <x> <z> [count] [radius] | world list | world clear" );
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
