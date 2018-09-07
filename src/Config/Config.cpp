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
		m_ScreenDepth = configFile.get<float>( "General", "ScreenDepth" );
		m_ScreenNear = configFile.get<float>( "General", "ScreenNear" );
		m_screenWidth = configFile.get<float>( "General", "ScreenWidth" );
		m_screenHeight = configFile.get<float>( "General", "ScreenHeight" );
		m_MSAAQuality = configFile.get<int32_t>( "General", "MSAAQuality" );

		return true;
	}

	return false;
}