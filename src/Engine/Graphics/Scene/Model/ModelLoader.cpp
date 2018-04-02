#include "ModelLoader.h"
#include <fstream>
#include "../../System.h"



namespace GS
{


ModelLoader::ModelLoader()
{
}


ModelLoader::~ModelLoader()
{
}

DMModel&& ModelLoader::loadFromFile( const std::wstring& filename, uint32_t id )
{

	wchar_t buffer[1024];
	memset( buffer, 0, sizeof( wchar_t ) * 1024 );
	DWORD size;

	//std::wstring level_file = L"data\\\\level01.ini";
	std::wstring level_file = filename;

	//GetPrivateProfileSection( L"General", out, 500, level_file.data() );
	int lods = GetPrivateProfileInt( L"General", L"LOD", 0, level_file.data() );
	GetPrivateProfileString( L"General", L"Name", L"", buffer, 1024, level_file.data() );
	std::wstring model_name( buffer );

	DMModel model( id, model_name );

	size_t lod_counter = 0;
	// загружаем все лоды
	while( lod_counter <= lods )
	{
		int lods = GetPrivateProfileInt( model_name.data(), L"LOD", 0, level_file.data() );

		// загружаем все лоды модели
		for( size_t i = 0; i < lods; i++ )
		{
			std::wstring currentLOD = L"LOD" + std::to_wstring( i );

			int range = GetPrivateProfileInt( L"General", L"Range", 0, level_file.data() );
			
			memset( buffer, 0, sizeof( wchar_t ) * 1024 );
			GetPrivateProfileString( currentLOD.data(), L"Mesh", L"", buffer, 1024, level_file.data() );
			std::wstring meshName( buffer );

			memset( buffer, 0, sizeof( wchar_t ) * 1024 );
			GetPrivateProfileString( currentLOD.data(), L"Material", L"", buffer, 1024, level_file.data() );
			std::wstring matName( buffer );

			model.addLod( range, System::meshes().id( meshName ), System::materials().id( matName ) );
		}
	}

	return std::move( model );
}

}