#include "PropertyCommands.h"
#include <cstdio>
#include <cstdlib>
#include "Utils\utilites.h"

namespace GS
{

namespace
{

std::string formatFloats( const float* values, size_t count )
{
	std::string text;
	char buffer[32];
	for( size_t i = 0; i < count; ++i )
	{
		std::snprintf( buffer, sizeof( buffer ), "%g", values[i] );
		text += ( i ? "," : "" ) + std::string( buffer );
	}
	return text;
}

std::string toString( Property& property )
{
	switch( property.valueType() )
	{
		case ValueType::BOOL:
			return property.data<bool>() ? "true" : "false";
		case ValueType::FLOAT:
			return formatFloats( &property.data<float>(), 1 );
		case ValueType::VECTOR2:
			return formatFloats( &property.data<XMFLOAT2>().x, 2 );
		case ValueType::VECTOR3:
			return formatFloats( &property.data<XMFLOAT3>().x, 3 );
		case ValueType::VECTOR4:
			return formatFloats( &property.data<XMFLOAT4>().x, 4 );
		case ValueType::INT:
			return std::to_string( property.data<int32_t>() );
		case ValueType::UINT:
			return std::to_string( property.data<uint32_t>() );
	}
	return {};
}

// count чисел через запятую, как «x,y,z» в -camera
bool parseFloats( const std::string& text, float* values, size_t count )
{
	std::vector<std::string> parts;
	str_split( text, parts, "," );
	if( parts.size() != count )
		return false;
	for( size_t i = 0; i < count; ++i )
	{
		char* end = nullptr;
		values[i] = std::strtof( parts[i].c_str(), &end );
		if( end == parts[i].c_str() || *end != '\0' )
			return false;
	}
	return true;
}

bool fromString( Property& property, const std::string& text )
{
	switch( property.valueType() )
	{
		case ValueType::BOOL:
		{
			const bool on = text == "true" || text == "1" || text == "on";
			if( !on && text != "false" && text != "0" && text != "off" )
				return false;
			property.setData( on );
			return true;
		}
		case ValueType::FLOAT:
		{
			float value;
			return parseFloats( text, &value, 1 ) && ( property.setData( value ), true );
		}
		case ValueType::VECTOR2:
		{
			XMFLOAT2 value;
			return parseFloats( text, &value.x, 2 ) && ( property.setData( value ), true );
		}
		case ValueType::VECTOR3:
		{
			XMFLOAT3 value;
			return parseFloats( text, &value.x, 3 ) && ( property.setData( value ), true );
		}
		case ValueType::VECTOR4:
		{
			XMFLOAT4 value;
			return parseFloats( text, &value.x, 4 ) && ( property.setData( value ), true );
		}
		case ValueType::INT:
		case ValueType::UINT:
		{
			char* end = nullptr;
			const long value = std::strtol( text.c_str(), &end, 10 );
			if( end == text.c_str() || *end != '\0' )
				return false;
			if( property.valueType() == ValueType::INT )
				property.setData( static_cast<int32_t>( value ) );
			else
				property.setData( static_cast<uint32_t>( value ) );
			return true;
		}
	}
	return false;
}

// Путь «окно/подокно/свойство»: container — последнее найденное окно, property — свойство (если путь на нём кончается)
struct Resolved
{
	PropertyContainer* container = nullptr;
	Property* property = nullptr;
};

bool resolve( const std::vector<PropertyContainer*>& roots, const std::string& path, Resolved& resolved )
{
	std::vector<std::string> parts;
	str_split( path, parts, "/" );
	const std::vector<PropertyContainer*>* level = &roots;
	for( size_t i = 0; i < parts.size(); ++i )
	{
		PropertyContainer* next = nullptr;
		for( PropertyContainer* container : *level )
		{
			if( container->name() == parts[i] )
			{
				next = container;
				break;
			}
		}
		if( next )
		{
			resolved.container = next;
			level = &next->subContainer();
			continue;
		}
		// Не окно — свойство, и только последней частью пути
		if( i + 1 == parts.size() && resolved.container && resolved.container->exists( parts[i] ) )
		{
			resolved.property = &resolved.container->property( parts[i] );
			return true;
		}
		return false;
	}
	return !parts.empty();
}

}

void registerPropertyCommands( ConsoleCommands& commands, std::function<const std::vector<PropertyContainer*>&()> roots )
{
	commands.registerCommand( "list", "[window/subwindow] - property windows, their subwindows and properties",
							  [roots]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.empty() )
		{
			for( PropertyContainer* container : roots() )
				reply->line( container->name() + "/" );
			return reply->ok();
		}
		Resolved resolved;
		if( !resolve( roots(), args[0], resolved ) || resolved.property )
			return reply->error( "no window " + args[0] );
		for( PropertyContainer* sub : resolved.container->subContainer() )
			reply->line( sub->name() + "/" );
		for( const std::string& name : resolved.container->names() )
			reply->line( name + " = " + toString( resolved.container->property( name ) ) );
		reply->ok();
	} );

	commands.registerCommand( "get", "<window/property> - property value",
							  [roots]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		Resolved resolved;
		if( args.empty() || !resolve( roots(), args[0], resolved ) || !resolved.property )
			return reply->error( "no property " + ( args.empty() ? std::string() : args[0] ) );
		reply->ok( toString( *resolved.property ) );
	} );

	commands.registerCommand( "set", "<window/property> <value> - set a property as the GUI does (bool, number, x,y,z)",
							  [roots]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		Resolved resolved;
		if( args.size() < 2 || !resolve( roots(), args[0], resolved ) || !resolved.property )
			return reply->error( "no property " + ( args.empty() ? std::string() : args[0] ) );
		if( !fromString( *resolved.property, args[1] ) )
			return reply->error( "can`t parse " + args[1] + " as the property type" );
		reply->ok( toString( *resolved.property ) );
	} );
}

}
