#include "ImageFile.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <tinyexr.h>
#include "Utils\utilites.h"
#include "Logger\Logger.h"

namespace
{

// OpenEXR через tinyexr: RGBA float (каналов меньше — tinyexr дополняет), как LoadFromHDRFile у DirectXTex.
// Файл читается через широкий путь, а разбирается из памяти: fopen в tinyexr не знает UTF-8. Сжатие DWAA / DWAB
// tinyexr v1 не читает — файл не загрузится, причина — в логе
HRESULT loadFromEXRFile( const std::wstring& filename, ScratchImage& image )
{
	std::ifstream file( filename, std::ios::binary );
	if( !file )
		return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
	const std::vector<unsigned char> data( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );

	float* rgba = nullptr;
	int width = 0;
	int height = 0;
	const char* error = nullptr;
	if( LoadEXRFromMemory( &rgba, &width, &height, data.data(), data.size(), &error ) != TINYEXR_SUCCESS )
	{
		LOG( std::string( "tinyexr: " ) + ( error ? error : "unknown error" ) );
		FreeEXRErrorMessage( error );
		return E_FAIL;
	}

	HRESULT hr = image.Initialize2D( DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, 1, 1 );
	if( SUCCEEDED( hr ) )
	{
		const Image* target = image.GetImage( 0, 0, 0 );
		for( int y = 0; y < height; ++y )
			memcpy( target->pixels + y * target->rowPitch, rgba + static_cast<size_t>( y ) * width * 4, width * 4 * sizeof( float ) );
	}
	free( rgba );
	return hr;
}

}

namespace ImageFile
{

bool load( const std::string& path, ScratchImage& image )
{
	std::wstring wideFilename = utf8ToWide( path );

	wchar_t ext[_MAX_EXT];
	_wsplitpath_s( wideFilename.data(), nullptr, 0, nullptr, 0, nullptr, 0, ext, _MAX_EXT );

	HRESULT hr;
	if( _wcsicmp( ext, L".dds" ) == 0 )
	{
		hr = LoadFromDDSFile( wideFilename.data(), DDS_FLAGS_NONE, nullptr, image );
	}
	else if( _wcsicmp( ext, L".tga" ) == 0 )
	{
		hr = LoadFromTGAFile( wideFilename.data(), nullptr, image );
	}
	else if( _wcsicmp( ext, L".hdr" ) == 0 )
	{
		hr = LoadFromHDRFile( wideFilename.data(), nullptr, image );
	}
	else if( _wcsicmp( ext, L".exr" ) == 0 )
	{
		hr = loadFromEXRFile( wideFilename, image );
	}
	else
	{
		hr = LoadFromWICFile( wideFilename.data(), WIC_FLAGS_NONE, nullptr, image );
	}

	return SUCCEEDED( hr );
}

}

