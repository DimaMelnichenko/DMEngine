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

// Разрешение окна: desktop — 0 × 0 (весь монитор), иначе «ширинаxвысота» (1920x1080); не разобрано — false, без изменений
bool parseResolution( const std::string& text, uint32_t& width, uint32_t& height )
{
	if( text == "desktop" )
	{
		width = height = 0;
		return true;
	}
	unsigned w = 0, h = 0;
	char separator = 0;
	if( sscanf_s( text.c_str(), "%u%c%u", &w, &separator, 1, &h ) != 3 || ( separator != 'x' && separator != 'X' ) || w == 0 || h == 0 )
		return false;
	width = w;
	height = h;
	return true;
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
		m_vSync = iniBool( file, "General", "VSync" );
		m_screenDepth = iniFloat( file, "General", "ScreenDepth" );
		m_screenNear = iniFloat( file, "General", "ScreenNear" );
		parseResolution( iniString( file, "General", "Resolution" ), m_windowWidth, m_windowHeight );
		if( const int32_t resolution = iniInt( file, "General", "ShadowMapResolution" ); resolution > 0 )
			m_shadowMapResolution = static_cast<uint32_t>( resolution );
		m_depthPrepass = iniString( file, "General", "DepthPrepass" ) != "false";
		m_gpuValidation = iniString( file, "General", "GpuValidation" ) == "true";

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
		else if( args[i] == "-noparticles" )
		{
			m_particles = false;
		}
		else if( args[i] == "-freezetime" )
		{
			m_gameTimeRunning = false;
		}
		else if( args[i] == "-remote" )
		{
			m_remoteControl = true;
		}
		else if( args[i] == "-pix" )
		{
			m_pixCapture = true;
		}
		else if( args[i] == "-gpuvalidation" )
		{
			m_gpuValidation = true;
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
		else if( args[i] == "-resolution" )
		{
			parseResolution( args[i + 1], m_windowWidth, m_windowHeight );
		}
	}
}
