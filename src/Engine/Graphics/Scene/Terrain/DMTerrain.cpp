#include "DMTerrain.h"
#include "System.h"
#include "json.hpp"
#include <fstream>
#include "Shaders\DMClipMapShader.h"

DMTerrain::DMTerrain( ) 
{
}

DMTerrain::~DMTerrain( )
{
	
}

bool DMTerrain::Initialize( const std::string& terrainConf )
{
	GS::System::materials().load( "GeoClipMap" );

	using json = nlohmann::json;

	json jsonParser;

	try
	{
		std::ifstream jsonFile( terrainConf.data() );
		jsonFile.is_open();
		jsonFile >> jsonParser;
		jsonFile.close();
	}
	catch( std::exception& e )
	{
		return false;
	}

	
	if( !loadHeightMap( jsonParser["heightmap"], 
						jsonParser["width_multipler"], 
						jsonParser["height_multipler"], 
						jsonParser["wireframe_size"],
						jsonParser["steps"] ) )
		return false;

	m_normalMapName = jsonParser["normalmap"];

	if( !GS::System::textures().load( m_normalMapName ) )
		return false;

	m_clip_map.setMapScale( jsonParser["map_scale"] );

	if( jsonParser.count( "hight_offset" ) )
		m_clip_map.setHightOffset( jsonParser["hight_offset"] );
	else
		m_clip_map.setHightOffset( 0.0 );

	return true;
}

bool DMTerrain::loadHeightMap( const std::string& file_name, float width_multipler, float height_multipler, float wireframeSize, int steps )
{
	m_widthMultipler = width_multipler;
	m_height_map.LoadMap( file_name );

	//int steps = static_cast<int>( max( m_height_map.size().x * width_multipler / wireframeSize, 2 ) );
	//steps = (int)pow( steps, 0.5f );
	
	m_clip_map.Initialize( wireframeSize, steps, m_height_map.size().x, width_multipler, height_multipler );
	//m_clip_map.Initialize( 63, 4, m_height_map.size().x * width_multipler, height_multipler );

	return  true;
}

void DMTerrain::Render( const DMCamera& camera, const DMFrustum& frustum )
{
	static DMClipMapShader::PSParamBuffer ps_param;
	ps_param.base_normal_multipler = 1.1f;
	for( size_t i = 0; i < 8; i++ )
	{
		ps_param.texture_tile[i] = 1.0f;
	}
	
	ps_param.base_normal_multipler = m_clip_map.heightMultipler();
	ps_param.camera_position = camera.position();
	
	DMClipMapShader* shader = dynamic_cast<DMClipMapShader*>( GS::System::materials().get( "GeoClipMap" )->m_shader.get() );
	shader->SetPSParameters( &ps_param );
	shader->selectPhase( 0 );

	ID3D11ShaderResourceView* height_srv = m_height_map.map();
	DMD3D::instance().GetDeviceContext()->VSSetShaderResources( 0, 1, &height_srv );
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 1, &height_srv );
	ID3D11ShaderResourceView* normal_srv = GS::System::textures().get( m_normalMapName )->GetTexture();
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 1, 1, &normal_srv );
	//m_clip_map_shader.SetTextures( m_terrain_textures.count(), m_terrain_textures.GetTextureArray() );

	m_clip_map.Render( *shader, camera, frustum );
}

const DMHeightMap& DMTerrain::heightmap()
{
	return m_height_map;
}
