#include "Config.h"
#include <cstdlib>
#include <fstream>
#include <Windows.h>
#include "Utils\utilites.h"

namespace
{

// Значение ключа param секции [block] файла settings.ini (GetPrivateProfile*): нет ключа — пустая строка или 0
std::string iniString( const std::string& file, const char* block, const char* param )
{
	char buffer[1024] = {};
	GetPrivateProfileString( block, param, "", buffer, sizeof( buffer ), file.c_str() );
	return buffer;
}

int32_t iniInt( const std::string& file, const char* block, const char* param )
{
	return static_cast<int32_t>( GetPrivateProfileInt( block, param, 0, file.c_str() ) );
}

bool iniBool( const std::string& file, const char* block, const char* param )
{
	return iniString( file, block, param ) == "true";
}

float iniFloat( const std::string& file, const char* block, const char* param )
{
	const std::string value = iniString( file, block, param );
	return value.empty() ? 0.0f : static_cast<float>( std::atof( value.c_str() ) );
}

}


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
		m_fullScreen = iniBool( file, "General", "FullScreen" );
		m_vSync = iniBool( file, "General", "VSync" );
		m_screenDepth = iniFloat( file, "General", "ScreenDepth" );
		m_screenNear = iniFloat( file, "General", "ScreenNear" );
		m_screenWidth = iniFloat( file, "General", "ScreenWidth" );
		m_screenHeight = iniFloat( file, "General", "ScreenHeight" );
		m_backBufferWidth = iniFloat( file, "General", "BackBufferWidth" );
		m_backBufferHeight = iniFloat( file, "General", "BackBufferHeight" );
		if( const int32_t resolution = iniInt( file, "General", "ShadowMapResolution" ); resolution > 0 )
			m_shadowMapResolution = static_cast<uint32_t>( resolution );
		m_depthPrepass = iniString( file, "General", "DepthPrepass" ) != "false";
		m_gpuValidation = iniString( file, "General", "GpuValidation" ) != "false";

		strToVec3( iniString( file, "Camera", "Position" ), m_cameraPosition );
		strToVec2( iniString( file, "Camera", "Rotation" ), m_cameraRotation );
		m_levelName = iniString( file, "Level", "Name" );

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
		else if( args[i] == "-nomouse" )
		{
			m_mouseLook = false;
		}
		else if( args[i] == "-nowind" )
		{
			m_wind = false;
		}
		else if( args[i] == "-remote" )
		{
			m_remoteControl = true;
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
