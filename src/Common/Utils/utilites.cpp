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