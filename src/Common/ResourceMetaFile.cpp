#include "ResourceMetaFile.h"
#include <Windows.h>



ResourceMetaFile::ResourceMetaFile( const std::string& name ) : m_fileName( name )
{
}


ResourceMetaFile::~ResourceMetaFile()
{
}

std::string ResourceMetaFile::string( const std::string& block, const std::string& param )
{
	static char buffer[1024];
	memset( buffer, 0, sizeof( char ) * 1024 );
	GetPrivateProfileString( block.data(), param.data(), "", buffer, 1024, m_fileName.data() );
	return std::string( buffer );
}

int32_t ResourceMetaFile::number( const std::string& block, const std::string& param )
{
	return GetPrivateProfileInt( block.data(), param.data(), 0, m_fileName.data() );
}