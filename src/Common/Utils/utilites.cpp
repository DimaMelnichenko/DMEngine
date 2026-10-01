#include "utilites.h"
#include <charconv>
#include <cstdio>

using namespace DirectX;



void str_split( const std::string& str, std::vector<std::string>& tokens, const std::string& delimiters )
{
	std::string::size_type lastPos = str.find_first_not_of( delimiters, 0 );
	std::string::size_type pos = str.find_first_of( delimiters, lastPos );
	while( std::string::npos != pos || std::string::npos != lastPos )
	{
		tokens.push_back( str.substr( lastPos, pos - lastPos ) );
		lastPos = str.find_first_not_of( delimiters, pos );
		pos = str.find_first_of( delimiters, lastPos );
	}
}

std::wstring utf8ToWide( const std::string& str )
{
	if( str.empty() )
		return {};

	int size = MultiByteToWideChar( CP_UTF8, 0, str.data(), static_cast<int>( str.size() ), nullptr, 0 );
	std::wstring result( size, L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, str.data(), static_cast<int>( str.size() ), result.data(), size );
	return result;
}

bool strToVec2( const std::string& str, XMFLOAT2& vector )
{
	std::vector<std::string> vecString;
	str_split( str, vecString, ", \t" );

	if( vecString.size() < 2 )
		return false;

	if( auto[p, ec] = std::from_chars( vecString[0].data(), vecString[0].data() + vecString[0].size(), vector.x ); ec != std::errc() )
		return false;
	if( auto[p, ec] = std::from_chars( vecString[1].data(), vecString[1].data() + vecString[1].size(), vector.y ); ec != std::errc() )
		return false;

	return true;
}

bool strToVec3( const std::string& str, XMFLOAT3& vector )
{
	std::vector<std::string> vecString;
	str_split( str, vecString, ", \t" );

	if( vecString.size() < 3 )
		return false;

	if( auto[p, ec] = std::from_chars( vecString[0].data(), vecString[0].data() + vecString[0].size(), vector.x ); ec != std::errc() )
		return false;
	if( auto[p, ec] = std::from_chars( vecString[1].data(), vecString[1].data() + vecString[1].size(), vector.y ); ec != std::errc() )
		return false;
	if( auto[p, ec] = std::from_chars( vecString[2].data(), vecString[2].data() + vecString[2].size(), vector.z ); ec != std::errc() )
		return false;

	return true;
}

bool strToVec4( const std::string& str, XMFLOAT4& vector )
{
	std::vector<std::string> vecString;
	str_split( str, vecString, ", \t" );

	if( vecString.size() < 4 )
		return false;
		
	if( auto[p, ec] = std::from_chars( vecString[0].data(), vecString[0].data() + vecString[0].size(), vector.x ); ec != std::errc() ) 
		return false;
	if( auto[p, ec] = std::from_chars( vecString[1].data(), vecString[1].data() + vecString[1].size(), vector.y ); ec != std::errc() ) 
		return false;
	if( auto[p, ec] = std::from_chars( vecString[2].data(), vecString[2].data() + vecString[2].size(), vector.z ); ec != std::errc() ) 
		return false;
	if( auto[p, ec] = std::from_chars( vecString[3].data(), vecString[3].data() + vecString[3].size(), vector.w ); ec != std::errc() ) 
		return false;

	return true;
}

std::string vec4ToStr( const XMFLOAT4& vec )
{
	return std::to_string( vec.x ) + "," + std::to_string( vec.y ) + "," + std::to_string( vec.z ) + "," + std::to_string( vec.w );
}

std::string vec3ToStr( const XMFLOAT3& vec )
{
	char text[96];
	std::snprintf( text, sizeof( text ), "%g,%g,%g", vec.x, vec.y, vec.z );
	return text;
}
