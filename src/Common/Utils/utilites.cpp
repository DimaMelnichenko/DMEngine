#include "utilites.h"


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


D3DXVECTOR3 strToVec3( const std::string& str )
{
	std::vector<std::string> vec3String;
	str_split( str, vec3String, "," );

	if( vec3String.size() )
		return D3DXVECTOR3( ::atof( vec3String[0].c_str() ), ::atof( vec3String[1].c_str() ), ::atof( vec3String[2].c_str() ) );

	return D3DXVECTOR3( 0.0, 0.0, 0.0 );
}

D3DXVECTOR4 strToVec4( const std::string& str )
{
	std::vector<std::string> vecString;
	str_split( str, vecString, "," );

	if( vecString.size() )
		return D3DXVECTOR4( ::atof( vecString[0].c_str() ), 
							::atof( vecString[1].c_str() ), 
							::atof( vecString[2].c_str() ),
							::atof( vecString[3].c_str() ) );

	return D3DXVECTOR4( 0.0, 0.0, 0.0, 1.0 );
}