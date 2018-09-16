#include "TextureLoader.h"
#include <locale>
#include <codecvt>


TextureLoader::TextureLoader()
{
}


TextureLoader::~TextureLoader()
{
}

bool TextureLoader::loadFromFile( const char* filename, ScratchImage& image )
{
	HRESULT result;

	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	//std::string narrow = converter.to_bytes( wide_utf16_source_string );
	std::wstring wideFilename = converter.from_bytes( filename );

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
	else
	{
		hr = LoadFromWICFile( wideFilename.data(), WIC_FLAGS_NONE, nullptr, image );
	}

	if( FAILED( hr ) )
	{
		return false;
	}

	return true;
}
