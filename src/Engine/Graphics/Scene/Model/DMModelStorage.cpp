#include "DMModelStorage.h"
#include <Windows.h>
#include <string>
#include "DMModel.h"
#include "System.h"



DMModelStorage::DMModelStorage()
{
}


DMModelStorage::~DMModelStorage()
{
}

bool DMModelStorage::initModels()
{
	wchar_t out[500];
	memset( out, 0, sizeof( wchar_t ) * 500 );
	DWORD size;

	std::wstring level_file = L"data\\level01.ini";

	GetPrivateProfileSection( L"Models", out, 500, level_file.data() );

	size_t offset_start = 0;

	// загружаем все модели
	while( wcslen( &out[offset_start] ) > 0 )
	{
		std::wstring model_name = ( &out[offset_start] );

		DMModel model( model_name );

		int lods = GetPrivateProfileInt( model_name.data(), L"LOD", 0, level_file.data() );

		// загружаем все лоды модели
		for( size_t i = 0; i < lods; i++ )
		{
			std::wstring model_lod = L"model_lod" + std::to_wstring( i );
			wchar_t model_lod_path[1024];
			GetPrivateProfileString( model_name.data(), model_lod.data(), L"", model_lod_path, 1024, level_file.data() );
			int dist = 0;
			model_lod = L"dist_lod" + std::to_wstring( i );
			dist = GetPrivateProfileInt( model_name.data(), model_lod.data(), 0, level_file.data() );
			if( wcslen( model_lod_path ) && dist > 0 )
			{
				if( model.countOfLOD() == 0 )
				{
					model.Initialize( dist, DMMesh::VertexCombination::V_PTNTB, model_lod_path );
				}
				else
				{
					model.addLODModel( dist, DMMesh::VertexCombination::V_PTNTB, model_lod_path );
				}
			}
		}

		if( lods && model.countOfLOD() )
		{
			wchar_t albedo[1024];
			wchar_t normal[1024];
			wchar_t height[1024];
			wchar_t gim[1024];
			GetPrivateProfileString( model_name.data(), L"albedo", L"", albedo, 1024, level_file.data() );
			GetPrivateProfileString( model_name.data(), L"normal", L"", normal, 1024, level_file.data() );
			GetPrivateProfileString( model_name.data(), L"height", L"", height, 1024, level_file.data() );
			GetPrivateProfileString( model_name.data(), L"gim", L"", gim, 1024, level_file.data() );
			if( wcslen( albedo ) > 0 )					
				model.setTexure( DMModel::albedo, GS::System::textures().id( albedo ) );
			if( wcslen( normal ) > 0 )
				model.setTexure( DMModel::TextureType::normal, GS::System::textures().id( normal ) );
			if( wcslen( height ) > 0 )
				model.setTexure( DMModel::TextureType::height, GS::System::textures().id( height ) );
			if( wcslen( gim ) > 0 )
				model.setTexure( DMModel::TextureType::gim, GS::System::textures().id( gim ) );

			int scale = GetPrivateProfileInt( model_name.data(), L"scale", 0, level_file.data() );

			if( scale )
			{
				//model.setScale( (float)scale / 100.0f );
			}

			int x = GetPrivateProfileInt( model_name.data(), L"pos_x", 0, level_file.data() );
			int y = GetPrivateProfileInt( model_name.data(), L"pos_y", 0, level_file.data() );
			int z = GetPrivateProfileInt( model_name.data(), L"pos_z", 0, level_file.data() );

			//model.setPos( (float)x / 100.0f, (float)y / 100.0f, (float)z / 100.0f );

			wchar_t shader[200];
			GetPrivateProfileString( model_name.data(), L"shader", L"", shader, 200, level_file.data() );
			if( wcslen( shader ) )
			{
				//m_render_queues[shader]->append( model.get() );
				//model.addRenderQueue( &m_render_queues[shader] );
			}

			//model.addRenderQueue( &m_render_queues[L"shadow"] );

			//m_models[model_name] = std::move( model );
		}

		offset_start += wcslen( &out[offset_start] );
		offset_start += 1;
	}
}