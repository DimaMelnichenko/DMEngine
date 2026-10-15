#include "Viewport.h"
#include "GUI.h"
#include "Engine\Console\ConsoleCommands.h"
#include "Scene\Camera\DMCamera.h"
#include "Scene\Scene.h"
#include "Scene\Water\WaterSimulation.h"

using namespace GS;

void Viewport::registerCommands( ConsoleCommands& console, DMCamera& camera, Scene& scene, GUI& gui )
{
	console.registerCommand( "pick", "<x> <y> - select under a window point, as a click in the viewport (a stream curve point in "
							 "the Stream curves mode, a light or particle emitter icon, else a model instance)",
							 [this, &camera, &scene, &gui]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.size() != 2 )
			return reply->error( "usage: pick <x> <y>" );
		const std::string picked = clickAt( camera, scene, gui, std::stof( args[0] ), std::stof( args[1] ) );
		reply->ok( picked.empty() ? "nothing" : picked );
	} );
	console.registerCommand( "brush", "<x> <y> [<x> <y> ...] [shift] [ctrl] - a stroke of the terrain brush (Paint of the terrain window) "
							 "through window points, as the mouse with the left button held in the Terrain paint mode; undoable",
							 [this, &camera, &scene, &gui]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		std::vector<ImVec2> points;
		bool shift = false, ctrl = false;
		std::vector<float> numbers;
		for( const std::string& arg : args )
		{
			if( arg == "shift" )
				shift = true;
			else if( arg == "ctrl" )
				ctrl = true;
			else
			{
				try
				{
					numbers.push_back( std::stof( arg ) );
				}
				catch( const std::exception& )
				{
					return reply->error( "usage: brush <x> <y> [<x> <y> ...] [shift] [ctrl]" );
				}
			}
		}
		if( numbers.size() < 2 || numbers.size() % 2 )
			return reply->error( "usage: brush <x> <y> [<x> <y> ...] [shift] [ctrl]" );
		for( size_t i = 0; i < numbers.size(); i += 2 )
			points.emplace_back( numbers[i], numbers[i + 1] );
		const std::string result = brushStroke( camera, scene.terrainPaint(), scene.terrainPipeline(), gui, points, shift, ctrl );
		if( result.rfind( "error: ", 0 ) == 0 )
			reply->error( result.substr( 7 ) );
		else
			reply->ok( result );
	} );
	console.registerCommand( "stream", "move <x,z> | insert | delete | falloff <m> - edit the selected stream curve point (pick it "
							 "in the Stream curves mode), as the gizmo, Insert / Delete and Falloff in the viewport",
							 [this, &scene]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.empty() || !scene.water().initialized() )
			return reply->error( "usage: stream move <x,z> | insert | delete (the level needs water)" );
		StreamCurves& streams = scene.water().streams();
		bool done = false;
		if( args[0] == "move" && args.size() == 2 )
		{
			const size_t comma = args[1].find( ',' );
			if( comma == std::string::npos )
				return reply->error( "usage: stream move <x,z>" );
			done = moveStreamPoint( streams, std::stof( args[1].substr( 0, comma ) ), std::stof( args[1].substr( comma + 1 ) ) );
		}
		else if( args[0] == "insert" )
			done = insertStreamPoint( streams );
		else if( args[0] == "delete" )
			done = removeStreamPoint( streams );
		else if( args[0] == "falloff" && args.size() == 2 )
		{
			setStreamFalloff( std::stof( args[1] ) );
			return reply->ok();
		}
		else
			return reply->error( "usage: stream move <x,z> | insert | delete" );
		if( done )
			reply->ok();
		else
			reply->error( "stream: select a stream curve point first (pick in the Stream curves mode)" );
	} );
}
