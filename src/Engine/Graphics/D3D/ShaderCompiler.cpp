#include "ShaderCompiler.h"
#include "GpuResources.h"
#include "DirectX.h"
#include <dxcapi.h>
#include <d3d12shader.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "Logger\Logger.h"
#include "Utils\utilites.h"
#include "Utils\Fingerprint.h"

namespace
{

constexpr const char* cacheDirectory = "cache/shaders";
constexpr const char* shadersDirectory = "Shaders";

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

// Флаги шейдеров, которые генерируют данные (reproducible): одинаковые в обеих конфигурациях, строгий IEEE (-Gis)
const wchar_t* const reproducibleArguments[] =
{
	L"-O3", L"-Gis", L"-Qstrip_reflect", L"-HV", L"2021",
};

uint32_t alignUp( uint32_t value, uint32_t alignment )
{
	return ( value + alignment - 1 ) / alignment * alignment;
}

// Тип поля в C++ (shader_constants.h): скаляры и векторы — по 4 байта без выравнивания, матрица 4 × 4 — XMMATRIX
// (выравнивание 16), структура — alignas( 16 ) с теми же полями подряд
struct CppField
{
	uint32_t size = 0;
	uint32_t alignment = 4;
	std::string problem;	// раскладка внутри структуры расходится с HLSL или у типа нет пары в C++
};

// Следующее поле name типа type — в C++ после end: смещение там должно совпасть с HLSL (offset); false — нет (problem)
bool placeField( ID3D12ShaderReflectionType* type, const std::string& name, uint32_t offset, uint32_t& end, std::string& problem );

CppField cppField( ID3D12ShaderReflectionType* type, const std::string& name )
{
	CppField field;
	D3D12_SHADER_TYPE_DESC desc = {};
	if( FAILED( type->GetDesc( &desc ) ) )
		field.problem = name + ": no reflection";
	else if( desc.Class == D3D_SVC_SCALAR || desc.Class == D3D_SVC_VECTOR )
		field.size = 4 * desc.Columns;
	else if( ( desc.Class == D3D_SVC_MATRIX_COLUMNS || desc.Class == D3D_SVC_MATRIX_ROWS ) && desc.Rows == 4 && desc.Columns == 4 )
	{
		field.size = 64;
		field.alignment = 16;
	}
	else if( desc.Class == D3D_SVC_STRUCT )
	{
		uint32_t end = 0;
		for( UINT i = 0; i < desc.Members && field.problem.empty(); ++i )
		{
			ID3D12ShaderReflectionType* member = type->GetMemberTypeByIndex( i );
			D3D12_SHADER_TYPE_DESC memberDesc = {};
			member->GetDesc( &memberDesc );
			placeField( member, name + "." + type->GetMemberTypeName( i ), memberDesc.Offset, end, field.problem );
		}
		field.size = alignUp( end, 16 );
		field.alignment = 16;
	}
	else
		field.problem = name + ": the type has no C++ counterpart in shader_constants.h";
	return field;
}

bool placeField( ID3D12ShaderReflectionType* type, const std::string& name, uint32_t offset, uint32_t& end, std::string& problem )
{
	const CppField field = cppField( type, name );
	D3D12_SHADER_TYPE_DESC desc = {};
	type->GetDesc( &desc );
	if( !field.problem.empty() )
		problem = field.problem;
	else if( desc.Elements > 0 && field.size % 16 != 0 )
		problem = name + ": array elements are 16 bytes apart in HLSL, " + std::to_string( field.size ) + " in C++ — use 4-component elements";
	else if( alignUp( end, field.alignment ) != offset )
		problem = name + " is at offset " + std::to_string( offset ) + " in HLSL and " + std::to_string( alignUp( end, field.alignment ) ) +
				  " in C++: a vector must not cross a 16-byte boundary — pad the previous fields";
	else
		end = offset + field.size * std::max( desc.Elements, 1u );
	return problem.empty();
}

// Чем раскладка буфера констант по правилам HLSL отличается от тех же полей подряд в C++; пусто — совпадает
std::string constantLayoutProblem( ID3D12ShaderReflectionConstantBuffer* buffer, const D3D12_SHADER_BUFFER_DESC& desc )
{
	std::string problem;
	uint32_t end = 0;	// конец предыдущего поля в C++
	for( UINT i = 0; i < desc.Variables && problem.empty(); ++i )
	{
		ID3D12ShaderReflectionVariable* variable = buffer->GetVariableByIndex( i );
		D3D12_SHADER_VARIABLE_DESC field = {};
		variable->GetDesc( &field );
		placeField( variable->GetType(), field.Name, field.StartOffset, end, problem );
	}
	if( problem.empty() && alignUp( end, 16 ) != desc.Size )
		problem = "size " + std::to_string( desc.Size ) + " in HLSL and " + std::to_string( alignUp( end, 16 ) ) + " in C++";
	return problem;
}

}

ShaderCompiler& ShaderCompiler::instance()
{
	static ShaderCompiler compiler;
	return compiler;
}

std::string ShaderCompiler::profile( ShaderStageType type )
{
	switch( type )
	{
		case ShaderStageType::vertex: return "vs_6_6";
		case ShaderStageType::pixel: return "ps_6_6";
		case ShaderStageType::geometry: return "gs_6_6";
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
	Fingerprint stamp( Fingerprint::fnvOffsetBasis );
	std::error_code error;
	std::vector<std::filesystem::directory_entry> entries;
	for( const auto& entry : std::filesystem::directory_iterator( shadersDirectory, error ) )
		if( entry.is_regular_file( error ) )
			entries.push_back( entry );
	std::sort( entries.begin(), entries.end(), []( const auto& a, const auto& b ) { return a.path() < b.path(); } );
	for( const auto& entry : entries )
	{
		stamp.add( entry.path().filename().string() );
		stamp.add( static_cast<uint64_t>( entry.file_size( error ) ) );
		stamp.add( static_cast<int64_t>( entry.last_write_time( error ).time_since_epoch().count() ) );
	}
	return stamp.value;
}

bool ShaderCompiler::compile( const std::string& file, const std::string& entry, const std::string& profile, const std::string& defines,
							  std::vector<uint8_t>& bytecode, bool reproducible )
{
	bytecode.clear();
	if( !initialize() )
		return false;

	// Кэш на диске
	Fingerprint key( m_stamp );
	key.add( file );
	key.add( entry );
	key.add( profile );
	key.add( defines );
	const std::vector<const wchar_t*> compileFlags = reproducible ?
		std::vector<const wchar_t*>( std::begin( reproducibleArguments ), std::end( reproducibleArguments ) ) :
		std::vector<const wchar_t*>( std::begin( buildArguments ), std::end( buildArguments ) );
	for( const wchar_t* argument : compileFlags )
		key.add( argument, wcslen( argument ) * sizeof( wchar_t ) );
	const std::filesystem::path cachePath = std::filesystem::path( cacheDirectory ) / ( hex( key.value ) + ".dxil" );
	{
		std::ifstream cached( cachePath, std::ios::binary );
		if( cached )
		{
			bytecode.assign( std::istreambuf_iterator<char>( cached ), std::istreambuf_iterator<char>() );
			if( !bytecode.empty() )
			{
				++m_fromCache;
				checkConstantLayouts( bytecode, file );
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
	for( const wchar_t* argument : compileFlags )
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
	checkConstantLayouts( bytecode, file );

	std::ofstream cached( cachePath, std::ios::binary );
	if( cached )
		cached.write( reinterpret_cast<const char*>( bytecode.data() ), static_cast<std::streamsize>( bytecode.size() ) );
	return true;
}

void ShaderCompiler::checkConstantLayouts( const std::vector<uint8_t>& bytecode, const std::string& file )
{
	const DxcBuffer buffer = { bytecode.data(), bytecode.size(), 0 };
	ID3D12ShaderReflection* reflection = nullptr;
	if( FAILED( m_utils->CreateReflection( &buffer, __uuidof( ID3D12ShaderReflection ), reinterpret_cast<void**>( &reflection ) ) ) )
		return;		// отражение вырезано (Release, шейдеры reproducible)
	D3D12_SHADER_DESC desc = {};
	if( SUCCEEDED( reflection->GetDesc( &desc ) ) )
	{
		for( UINT i = 0; i < desc.ConstantBuffers; ++i )
		{
			ID3D12ShaderReflectionConstantBuffer* constants = reflection->GetConstantBufferByIndex( i );
			D3D12_SHADER_BUFFER_DESC constantsDesc = {};
			// Буфер, поля которого шейдер не читает, приходит без имени и полей — его проверит другой шейдер
			if( FAILED( constants->GetDesc( &constantsDesc ) ) || constantsDesc.Type != D3D_CT_CBUFFER ||
				constantsDesc.Variables == 0 || !m_checkedLayouts.insert( constantsDesc.Name ).second )
				continue;
			const std::string problem = constantLayoutProblem( constants, constantsDesc );
			if( !problem.empty() )
				LOG( "Constant buffer " + std::string( constantsDesc.Name ) + " (" + file + ") differs from C++: " + problem );
		}
	}
	reflection->Release();
}

void ShaderCompiler::logSummary() const
{
	LOG( "Shaders: " + std::to_string( m_compiled ) + " compiled by DXC, " + std::to_string( m_fromCache ) + " from cache/shaders" +
		 ( m_checkedLayouts.empty() ? "" : ", constant buffer layouts checked against C++: " + std::to_string( m_checkedLayouts.size() ) ) );
}

uint64_t ShaderCompiler::shadersStamp()
{
	initialize();
	return m_stamp;
}
