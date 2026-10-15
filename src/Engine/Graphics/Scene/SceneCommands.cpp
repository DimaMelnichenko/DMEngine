#include "SceneCommands.h"
#include <algorithm>
#include <cstdio>
#include <iterator>
#include "Scene.h"
#include "Terrain\TerrainPaintTool.h"
#include "Terrain\TerrainPipeline.h"
#include "Water\WaterSimulation.h"

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
