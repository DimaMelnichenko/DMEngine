#include "DMGraphics.h"
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include "Engine/Input/Input.h"
#include "Logger\Logger.h"
#include "Utils\utilites.h"

using namespace DirectX;

namespace GS
{

// Команды движка: проходы кадра, камера, снимки, захват PIX, шаг времени, замер GPU, клавиши, окна ImGui, выход. Команды
// редактора (GUI::registerCommands), вьюпорта (Viewport::registerCommands) и подсистем сцены (registerSceneCommands) — у них
void DMGraphics::registerCommands()
{
	// Камера как BugItGo в UE: положение и поворот в градусах; смена плана — экспозиция сразу по новому виду
	m_console.registerCommand( "passes", "log the passes of the next frame with their targets, reads and writes (D3D/GpuPass.h)",
							   [this]( const std::vector<std::string>&, const ConsoleReplyPtr& reply )
	{
		DMD3D::instance().logPasses();
		reply->ok( "passes of the next frame are written to log.txt" );
	} );
	m_console.registerCommand( "camera", "[x,y,z[,pitch,yaw]] - move the camera (camera cut); no arguments - current camera",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		DMCamera& camera = m_cameraPool["main"];
		std::string joined;
		for( const std::string& arg : args )
			joined += ( joined.empty() ? "" : "," ) + arg;
		std::vector<std::string> parts;
		str_split( joined, parts, ", " );
		std::vector<float> values;
		for( const std::string& part : parts )
		{
			char* end = nullptr;
			values.push_back( std::strtof( part.c_str(), &end ) );
			if( end == part.c_str() || *end != '\0' )
				return reply->error( "camera: can`t parse " + part );
		}
		if( !values.empty() )
		{
			if( values.size() != 3 && values.size() != 5 )
				return reply->error( "camera: expected x,y,z or x,y,z,pitch,yaw" );
			const XMFLOAT2 rotation = values.size() == 5 ? XMFLOAT2( values[3], values[4] ) : camera.rotation();
			camera.setView( XMFLOAT3( values[0], values[1], values[2] ), rotation.x, rotation.y );
			m_renderer.cameraCut();
			m_framesSinceCut = 0;
		}
		char text[128];
		const XMFLOAT3& position = camera.position();
		const XMFLOAT2 rotation = camera.rotation();
		std::snprintf( text, sizeof( text ), "%g,%g,%g,%g,%g", position.x, position.y, position.z, rotation.x, rotation.y );
		reply->ok( text );
	} );

	m_console.registerCommand( "screenshot", "<file.png|.jpg> [gui] [frames] - back buffer after the frame settles; gui - with "
							   "ImGui windows; frames - that many consecutive frames, file_00, file_01...",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.empty() )
			return reply->error( "screenshot: expected a file name" );
		ScreenshotRequest request{ utf8ToWide( args[0] ), false, reply };
		for( size_t i = 1; i < args.size(); ++i )
		{
			if( args[i] == "gui" )
			{
				request.withGui = true;
				continue;
			}
			const int frames = std::atoi( args[i].c_str() );
			if( frames <= 0 || frames > 1000 )
				return reply->error( "screenshot: expected gui or a number of frames 1..1000, got " + args[i] );
			request.frames = static_cast<uint32_t>( frames );
		}
		m_screenshots.push_back( std::move( request ) );
	} );

	// Захват PIX (движок с -pix, как -attachPIX и консоль UE): кадры после того, как кадр устоится, — в файл .wpix; ответ —
	// когда захват записан. Разбор — PIX (WinPix.exe) или pixtool: docs/d3d12.md §4
	m_console.registerCommand( "pix", "[file.wpix] [frames] - PIX GPU capture of the next frames after the frame settles (engine "
							   "with -pix); default captures\\<date_time>.wpix; the reply comes when the file is written",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( !DMD3D::instance().pixCaptureReady() )
			return reply->error( "pix: start the engine with -pix (PIX must be installed)" );
		std::filesystem::path path;
		if( !args.empty() )
			path = utf8ToWide( args[0] );
		else
		{
			char name[64];
			const std::time_t now = std::time( nullptr );
			std::tm local = {};
			localtime_s( &local, &now );
			std::strftime( name, sizeof( name ), "captures\\%Y%m%d_%H%M%S.wpix", &local );
			path = name;
		}
		const int frames = args.size() > 1 ? std::atoi( args[1].c_str() ) : 1;
		if( frames <= 0 || frames > 100 )
			return reply->error( "pix: expected 1..100 frames" );
		std::error_code error;
		path = std::filesystem::absolute( path, error );
		if( path.has_parent_path() )
			std::filesystem::create_directories( path.parent_path(), error );
		std::filesystem::remove( path, error );
		// Шаг 0 — ждать, пока кадр устоится, и начать захват; дальше — пока файл не появится и не перестанет расти
		struct State
		{
			int step = 0;
			uintmax_t size = 0;
			int stable = 0;
			int waited = 0;
		};
		auto state = std::make_shared<State>();
		m_console.addFrameTask( [this, reply, path, frames, state]
		{
			std::error_code error;
			if( state->step == 0 )
			{
				if( m_framesSinceCut < settleFrames )
					return false;
				if( !DMD3D::instance().captureFrames( path.wstring(), static_cast<uint32_t>( frames ) ) )
				{
					reply->error( "pix: capture is not started, see log.txt" );
					return true;
				}
				state->step = 1;
				return false;
			}
			if( ++state->waited > 3000 )
			{
				reply->error( "pix: no capture file after 3000 frames: " + path.string() );
				return true;
			}
			const uintmax_t size = std::filesystem::exists( path, error ) ? std::filesystem::file_size( path, error ) : 0;
			state->stable = size > 0 && size == state->size ? state->stable + 1 : 0;
			state->size = size;
			if( state->stable < 30 )
				return false;
			LOG( "PIX capture is written: " + path.string() + ", " + std::to_string( size / 1024 ) + " KB" );
			reply->ok( path.string() );
			return true;
		} );
	} );

	m_console.registerCommand( "timestep", "[seconds|off] - fixed frame time step (as -UseFixedTimeStep in UE); no arguments - current",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( !args.empty() )
		{
			if( args[0] == "off" )
				m_fixedTimeStep = 0.0f;
			else
			{
				const float step = static_cast<float>( std::atof( args[0].c_str() ) );
				if( step <= 0.0f || step > 1.0f )
					return reply->error( "timestep: expected seconds in (0, 1] or off" );
				m_fixedTimeStep = step;
			}
		}
		reply->ok( m_fixedTimeStep > 0.0f ? std::to_string( m_fixedTimeStep ) : "off" );
	} );

	m_console.registerCommand( "stat", "gpu [seconds=3] - average GPU time of the frame and passes, after the frame settles",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.empty() || args[0] != "gpu" )
			return reply->error( "stat: only stat gpu is supported" );
		const float seconds = args.size() > 1 ? static_cast<float>( std::atof( args[1].c_str() ) ) : 3.0f;
		if( seconds <= 0.0f )
			return reply->error( "stat gpu: expected a positive number of seconds" );
		m_console.addFrameTask( [this, reply, seconds]
		{
			if( m_framesSinceCut < settleFrames )
				return false;
			m_renderer.measureGpu( seconds, [reply]( const std::string& line ) { reply->ok( line ); } );
			return true;
		} );
	} );

	// Горячие клавиши (bindingKeys): имя — буква или цифра, как на клавиатуре, или скан-код DIK_*
	m_console.registerCommand( "key", "<letter|digit|scan code> - press a hotkey (G, Q, 1, 3, 4...)",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.empty() )
			return reply->error( "key: expected a key" );
		static const std::pair<const char*, uint8_t> rows[] = { { "1234567890", DIK_1 }, { "QWERTYUIOP", DIK_Q },
																 { "ASDFGHJKL", DIK_A }, { "ZXCVBNM", DIK_Z } };
		int code = -1;
		if( args[0].size() == 1 )
		{
			const char c = static_cast<char>( std::toupper( static_cast<unsigned char>( args[0][0] ) ) );
			for( const auto& [keys, first] : rows )
			{
				if( const char* found = std::strchr( keys, c ) )
					code = first + static_cast<int>( found - keys );
			}
		}
		if( code < 0 )
		{
			char* end = nullptr;
			const long value = std::strtol( args[0].c_str(), &end, 0 );
			if( end != args[0].c_str() && *end == '\0' && value > 0 && value < 256 )
				code = static_cast<int>( value );
		}
		if( code < 0 )
			return reply->error( "key: unknown key " + args[0] );
		if( !Input::instance().notifier().press( static_cast<uint8_t>( code ) ) )
			return reply->error( "key: nothing is bound to " + args[0] );
		reply->ok();
	} );

	m_console.registerCommand( "gui", "on|off - ImGui windows, as the G key", [this]( const std::vector<std::string>& args,
																					 const ConsoleReplyPtr& reply )
	{
		if( args.empty() || ( args[0] != "on" && args[0] != "off" ) )
			return reply->error( "gui: expected on or off" );
		m_showGUI = args[0] == "on";
		reply->ok();
	} );

	m_console.registerCommand( "quit", "- exit the engine normally (log is written to the end)",
							   [this]( const std::vector<std::string>&, const ConsoleReplyPtr& reply )
	{
		m_exitRequested = true;
		reply->ok();
	} );

}

}
