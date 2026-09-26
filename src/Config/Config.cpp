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
		m_backBufferWidth = configFile.get<float>( "General", "BackBufferWidth" );
		m_backBufferHeight = configFile.get<float>( "General", "BackBufferHeight" );
		m_MSAACount = configFile.get<int32_t>( "General", "MSAACount" );

		strToVec3( configFile.get<std::string>( "Camera", "Position" ), m_cameraPosition );
		strToVec2( configFile.get<std::string>( "Camera", "Rotation" ), m_cameraRotation );
		m_levelName = configFile.get<std::string>( "Level", "Name" );

		return true;
	}

	return false;
}

void Config::parseCommandLine( const std::string& commandLine )
{
	std::vector<std::string> args;
	str_split( commandLine, args, " \t" );

	for( size_t i = 0; i < args.size(); ++i )
	{
		if( args[i] == "-nogui" )
		{
			m_showGUI = false;
		}
		else if( i + 1 >= args.size() )
		{
			// Параметры ниже ждут значение
		}
		else if( args[i] == "-camera" )
		{
			// x,y,z — положение, необязательные pitch,yaw — поворот в градусах
			std::vector<std::string> values;
			str_split( args[i + 1], values, "," );
			strToVec3( args[i + 1], m_cameraPosition );
			if( values.size() >= 5 )
				strToVec2( values[3] + "," + values[4], m_cameraRotation );
		}
		else if( args[i] == "-level" )
		{
			m_levelName = args[i + 1];
		}
	}
}
