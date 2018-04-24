#pragma once
#include <string>


class ResourceMetaFile
{
public:
	ResourceMetaFile( const std::string& file );
	~ResourceMetaFile();

	std::string string( const std::string& block, const std::string& param );
	int32_t number( const std::string& block, const std::string& param );

private:
	std::string m_fileName;
};

