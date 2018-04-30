#include "Config.h"
#include "ResourceMetaFile.h"
#include <fstream>


Config::Config()
{
}


Config::~Config()
{
}

bool Config::readConfig( const std::string& file )
{
	std::ifstream f( file.c_str() );
	if( f.good() )
	{
		f.close();
		ResourceMetaFile configFile( file );

		m_FullScreen = configFile.get<bool>( "General", "FullScreen" );
		m_VSync = configFile.get<bool>( "General", "VSync" );
		m_ScreenDepth = configFile.get<bool>( "General", "ScreenDepth" );
		m_ScreenNear = configFile.get<bool>( "General", "ScreenNear" );

		return true;
	}

	return false;
}

bool Config::fullScreen()
{
	return m_FullScreen;
}

bool Config::vSync()
{
	return m_VSync;
}

float Config::ScreenDepth()
{
	return m_ScreenDepth;
}

float Config::ScreenNear()
{
	return m_ScreenNear;
}