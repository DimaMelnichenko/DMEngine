#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include <string>
#include <Windows.h>


class ResourceMetaFile
{
public:
	ResourceMetaFile( const std::string& file );
	~ResourceMetaFile();

	template<class Type>
	Type get( const std::string& block, const std::string& param );

	template<>
	std::string get( const std::string& block, const std::string& param )
	{
		static char buffer[1024];
		memset( buffer, 0, sizeof( char ) * 1024 );
		GetPrivateProfileString( block.data(), param.data(), "", buffer, 1024, m_fileName.data() );
		return std::string( buffer );
	}

	template<>
	int32_t get( const std::string& block, const std::string& param )
	{
		try
		{
			return GetPrivateProfileInt( block.data(), param.data(), 0, m_fileName.data() );
		}
		catch( ... )
		{

		}
		return 0;
	}
	
	template<>
	bool get( const std::string& block, const std::string& param )
	{
		return get<std::string>( block, param ) == "true" ? true : false;
	}

	template<>
	float get( const std::string& block, const std::string& param )
	{
		std::string num = get<std::string>( block, param );
		if( num != "" )
		{
			return static_cast<float>( ::atof( num.c_str() ) );
		}
		
		return 0.0f;
	}

private:
	std::string m_fileName;
};

