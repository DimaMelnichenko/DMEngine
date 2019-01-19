#include "utilites.h"
#include <charconv>

void delete_com( IUnknown* p )
{
	if( p )
	{
		p->Release();
		p = nullptr;
	}
}



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


XMFLOAT3 strToVec3( const std::string& str )
{
	std::vector<std::string> vec3String;
	str_split( str, vec3String, "," );

	if( vec3String.size() )
		return XMFLOAT3( ::atof( vec3String[0].c_str() ), ::atof( vec3String[1].c_str() ), ::atof( vec3String[2].c_str() ) );

	return XMFLOAT3( 0.0, 0.0, 0.0 );
}

bool strToVec4( const std::string& str, XMFLOAT4& vector )
{
	std::vector<std::string> vecString;
	str_split( str, vecString, "," );

	if( vecString.empty() )
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