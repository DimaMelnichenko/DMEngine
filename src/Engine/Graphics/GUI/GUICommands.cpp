#include "GUI.h"
#include "Engine\Console\ConsoleCommands.h"
#include "Engine\Console\PropertyCommands.h"

using namespace GS;

void GUI::registerCommands( ConsoleCommands& console )
{
	console.registerCommand( "action", "\"<name>\" - run an editor action, as File menu items (Save level, Exit)",
							 [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.size() != 1 )
		{
			std::string names;
			for( const Editor::Action& action : m_actions )
				names += " \"" + action.name + "\"";
			reply->error( "usage: action \"<name>\"; actions:" + names );
			return;
		}
		if( runAction( args[0] ) )
			reply->ok();
		else
			reply->error( "action: no action " + args[0] );
	} );
	console.registerCommand( "select", "\"<window>\" - select a properties window in the editor Outliner (Details shows it)",
							 [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.size() != 1 )
			return reply->error( "usage: select \"<window>\"" );
		if( select( args[0] ) )
			reply->ok();
		else
			reply->error( "select: no window " + args[0] );
	} );
	console.registerCommand( "toggle", "\"<name>\" - switch a View menu toggle (Stream curves, Wireframe...)",
							 [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.size() != 1 )
			return reply->error( "usage: toggle \"<name>\"" );
		for( const Editor::Toggle& toggle : m_toggles )
		{
			if( toggle.name == args[0] )
			{
				toggle.toggle();
				return reply->ok( toggle.state() ? "on" : "off" );
			}
		}
		reply->error( "toggle: no toggle " + args[0] );
	} );
	// История правок редактора, как Ctrl+Z / Ctrl+Y: ответ — имя отменённой или повторённой правки
	console.registerCommand( "undo", "- undo the last edit of the editor (Ctrl+Z)",
							 [this]( const std::vector<std::string>&, const ConsoleReplyPtr& reply )
	{
		std::string name;
		if( undo( &name ) )
			reply->ok( name );
		else
			reply->error( "undo: nothing to undo" );
	} );
	console.registerCommand( "redo", "- redo the last undone edit (Ctrl+Y)",
							 [this]( const std::vector<std::string>&, const ConsoleReplyPtr& reply )
	{
		std::string name;
		if( redo( &name ) )
			reply->ok( name );
		else
			reply->error( "redo: nothing to redo" );
	} );

	// Свойства окон: list, get, set, press (Engine/Console/PropertyCommands.h)
	registerPropertyCommands( console, [this]() -> const std::vector<PropertyContainer*>& { return m_containers; },
							  [this]( PropertyContainer& container, const std::string& button, std::string* name )
							  {
								  return press( container, button, name );
							  } );
}
