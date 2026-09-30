#include "ShaderCompiler.h"
#include "DirectX.h"
#include <dxcapi.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "Logger\Logger.h"
#include "Utils\utilites.h"

namespace
{

constexpr const char* cacheDirectory = "cache/shaders";
constexpr const char* shadersDirectory = "Shaders";

uint64_t fnv1a( const void* data, size_t size, uint64_t hash = 14695981039346656037ull )
{
	const uint8_t* bytes = static_cast<const uint8_t*>( data );
	for( size_t i = 0; i < size; ++i )
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

uint64_t fnv1a( const std::string& text, uint64_t hash )
{
	return fnv1a( text.data(), text.size(), hash );
}

std::string hex( uint64_t value )
{
	char buffer[17];
	snprintf( buffer, sizeof( buffer ), "%016llx", static_cast<unsigned long long>( value ) );
	return buffer;
}

// Флаги компиляции — часть ключа кэша: Debug и Release дают разный байткод
const wchar_t* const buildArguments[] =
{
#ifdef _DEBUG
	L"-Zi", L"-Qembed_debug", L"-Od",
#else
	L"-O3", L"-Qstrip_reflect",
#endif
	L"-HV", L"2021",
};

}

ShaderCompiler& ShaderCompiler::instance()
{
	static ShaderCompiler compiler;
	return compiler;
}

std::string ShaderCompiler::profile( SRVType type )
{
	switch( type )
	{
		case SRVType::vs: return "vs_6_6";
		case SRVType::ps: return "ps_6_6";
		case SRVType::gs: return "gs_6_6";
		case SRVType::hs: return "hs_6_6";
		case SRVType::ds: return "ds_6_6";
		default: return "cs_6_6";
	}
}

bool ShaderCompiler::initialize()
{
	if( m_initialized )
		return !m_failed;
	m_initialized = true;
	m_failed = true;

	if( FAILED( DxcCreateInstance( CLSID_DxcCompiler, __uuidof( IDxcCompiler3 ), reinterpret_cast<void**>( &m_compiler ) ) ) ||
		FAILED( DxcCreateInstance( CLSID_DxcUtils, __uuidof( IDxcUtils ), reinterpret_cast<void**>( &m_utils ) ) ) ||
		FAILED( m_utils->CreateDefaultIncludeHandler( &m_includeHandler ) ) )
	{
		LOG( "DXC is unavailable: dxcompiler.dll must be next to the executable" );
		return false;
	}
	if( !std::filesystem::exists( "dxil.dll" ) )
	{
		// Без валидатора DXC оставит байткод без подписи, и CreatePipelineState его отвергнет
		char path[MAX_PATH] = {};
		GetModuleFileNameA( nullptr, path, MAX_PATH );
		std::filesystem::path exeDir = std::filesystem::path( path ).parent_path();
		if( !std::filesystem::exists( exeDir / "dxil.dll" ) )
			LOG( "dxil.dll is not next to the executable: DXIL will not be signed and pipelines will fail to build" );
	}
	m_stamp = sourcesStamp();
	std::error_code error;
	std::filesystem::create_directories( cacheDirectory, error );
	m_failed = false;
	return true;
}

uint64_t ShaderCompiler::sourcesStamp() const
{
	// Отпечаток каталога шейдеров: любая правка меняет ключи всех шейдеров — include-зависимости не отслеживаются
	uint64_t hash = 14695981039346656037ull;
	std::error_code error;
	std::vector<std::filesystem::directory_entry> entries;
	for( const auto& entry : std::filesystem::directory_iterator( shadersDirectory, error ) )
		if( entry.is_regular_file( error ) )
			entries.push_back( entry );
	std::sort( entries.begin(), entries.end(), []( const auto& a, const auto& b ) { return a.path() < b.path(); } );
	for( const auto& entry : entries )
	{
		hash = fnv1a( entry.path().filename().string(), hash );
		const uint64_t size = static_cast<uint64_t>( entry.file_size( error ) );
		const int64_t time = entry.last_write_time( error ).time_since_epoch().count();
		hash = fnv1a( &size, sizeof( size ), hash );
		hash = fnv1a( &time, sizeof( time ), hash );
	}
	return hash;
}

bool ShaderCompiler::compile( const std::string& file, const std::string& entry, const std::string& profile, const std::string& defines,
							  std::vector<uint8_t>& bytecode )
{
	bytecode.clear();
	if( !initialize() )
		return false;

	// Кэш на диске
	uint64_t key = fnv1a( file, m_stamp );
	key = fnv1a( entry, key );
	key = fnv1a( profile, key );
	key = fnv1a( defines, key );
	for( const wchar_t* argument : buildArguments )
		key = fnv1a( argument, wcslen( argument ) * sizeof( wchar_t ), key );
	const std::filesystem::path cachePath = std::filesystem::path( cacheDirectory ) / ( hex( key ) + ".dxil" );
	{
		std::ifstream cached( cachePath, std::ios::binary );
		if( cached )
		{
			bytecode.assign( std::istreambuf_iterator<char>( cached ), std::istreambuf_iterator<char>() );
			if( !bytecode.empty() )
			{
				++m_fromCache;
				return true;
			}
		}
	}

	// Исходник — UTF-8 без BOM; имя файла первым аргументом: относительно него ищутся include, плюс каталог шейдеров
	IDxcBlobEncoding* sourceBlob = nullptr;
	const std::wstring wideFile = utf8ToWide( file );
	if( FAILED( m_utils->LoadFile( wideFile.c_str(), nullptr, &sourceBlob ) ) )
	{
		LOG( "Missing Shader File: " + file );
		return false;
	}
	DxcBuffer source = {};
	source.Ptr = sourceBlob->GetBufferPointer();
	source.Size = sourceBlob->GetBufferSize();
	source.Encoding = DXC_CP_UTF8;

	std::vector<std::wstring> ownedArguments = { wideFile, L"-E", utf8ToWide( entry ), L"-T", utf8ToWide( profile ), L"-I", utf8ToWide( shadersDirectory ) };
	for( const wchar_t* argument : buildArguments )
		ownedArguments.push_back( argument );
	std::vector<std::string> defineList;
	str_split( defines, defineList, "," );
	for( const std::string& define : defineList )
	{
		if( !define.empty() )
		{
			ownedArguments.push_back( L"-D" );
			ownedArguments.push_back( utf8ToWide( define ) );
		}
	}
	std::vector<const wchar_t*> arguments;
	for( const std::wstring& argument : ownedArguments )
		arguments.push_back( argument.c_str() );

	IDxcResult* result = nullptr;
	const HRESULT hr = m_compiler->Compile( &source, arguments.data(), static_cast<UINT32>( arguments.size() ), m_includeHandler,
											__uuidof( IDxcResult ), reinterpret_cast<void**>( &result ) );
	sourceBlob->Release();
	if( FAILED( hr ) || !result )
	{
		LOG( "DXC Compile call failed for " + file );
		return false;
	}

	// Предупреждения и ошибки — в лог; ошибки ещё в shader-error.txt, как раньше
	IDxcBlobUtf8* errors = nullptr;
	if( SUCCEEDED( result->GetOutput( DXC_OUT_ERRORS, __uuidof( IDxcBlobUtf8 ), reinterpret_cast<void**>( &errors ), nullptr ) ) && errors )
	{
		if( errors->GetStringLength() > 0 )
			LOG( "Shader " + file + " (" + entry + ", " + profile + ( defines.empty() ? "" : ", " + defines ) + "):\n" +
				 std::string( errors->GetStringPointer(), errors->GetStringLength() ) );
		errors->Release();
	}
	HRESULT status = E_FAIL;
	result->GetStatus( &status );
	if( FAILED( status ) )
	{
		std::ofstream errorFile( "shader-error.txt", std::ios::app );
		errorFile << "file: " << file << " entry: " << entry << " profile: " << profile << " defines: " << defines << "\n";
		LOG( "Error compiling shader. " + file );
		result->Release();
		return false;
	}

	IDxcBlob* object = nullptr;
	if( FAILED( result->GetOutput( DXC_OUT_OBJECT, __uuidof( IDxcBlob ), reinterpret_cast<void**>( &object ), nullptr ) ) || !object )
	{
		LOG( "DXC produced no object for " + file );
		result->Release();
		return false;
	}
	bytecode.assign( static_cast<const uint8_t*>( object->GetBufferPointer() ),
					 static_cast<const uint8_t*>( object->GetBufferPointer() ) + object->GetBufferSize() );
	object->Release();
	result->Release();
	++m_compiled;

	std::ofstream cached( cachePath, std::ios::binary );
	if( cached )
		cached.write( reinterpret_cast<const char*>( bytecode.data() ), static_cast<std::streamsize>( bytecode.size() ) );
	return true;
}

void ShaderCompiler::logSummary() const
{
	LOG( "Shaders: " + std::to_string( m_compiled ) + " compiled by DXC, " + std::to_string( m_fromCache ) + " from cache/shaders" );
}
