#include "DMTerrain.h"
#include "System.h"
#include "json.hpp"
#include <fstream>
#include "Shaders\DMClipMapShader.h"

DMTerrain::DMTerrain( ) :
	m_isInited(false)
{
}

DMTerrain::~DMTerrain( )
{
	
}

bool DMTerrain::Initialize( const std::string& terrainConf, const std::string& shaderName )
{
	m_shaderName = shaderName;

	GS::System::materials().load( m_shaderName );

	using json = nlohmann::json;

	json jsonParser;

	try
	{
		std::ifstream jsonFile( terrainConf.data() );
		jsonFile.is_open();
		jsonFile >> jsonParser;
		jsonFile.close();
	}
	catch( std::exception& )
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
	m_clip_map.setMapOffset( jsonParser["map_offset"][0], jsonParser["map_offset"][1] );
	m_clip_map.setMapOffsetSpeed( jsonParser["map_offset_speed"] );

	if( jsonParser.count( "hight_offset" ) )
		m_clip_map.setHightOffset( jsonParser["hight_offset"] );
	else
		m_clip_map.setHightOffset( 0.0 );

	
	m_materials.reserve( jsonParser.count("materials") );
	m_textures.reserve( jsonParser.count( "materials" ) );
	for( auto jsonMaterial : jsonParser["materials"] )
	{
		DMClipMapShader::Material material;
		material.baseColors = XMFLOAT3( jsonMaterial["baseColors"][0], jsonMaterial["baseColors"][1], jsonMaterial["baseColors"][2] );
		material.baseStartHeight = jsonMaterial["baseStartHeight"];
		material.baseBlends = jsonMaterial["baseBlends"];
		material.baseBlends = jsonMaterial["baseBlends"];
		material.textureScale = jsonMaterial["textureScale"];
		std::string textureName = jsonMaterial["Texture"];
		m_textures.push_back( GS::System::textures().get( textureName )->srv() );
		m_materials.push_back( material );
	}

	m_isInited = true;

	return true;
}

bool DMTerrain::loadHeightMap( const std::string& file_name, float width_multipler, float height_multipler, float wireframeSize, int steps )
{
	m_widthMultipler = width_multipler;
	m_height_map.LoadMap( file_name );

	//int steps = static_cast<int>( max( m_height_map.size().x * width_multipler / wireframeSize, 2 ) );
	//steps = (int)pow( steps, 0.5f );
	
	m_clip_map.Initialize( (int)wireframeSize, steps, m_height_map.size().x, width_multipler, height_multipler );
	//m_clip_map.Initialize( 63, 4, m_height_map.size().x * width_multipler, height_multipler );

	return  true;
}

void DMTerrain::Render( const DMCamera& camera, const DMFrustum& frustum )
{
	if( !m_isInited )
		return;

	static DMClipMapShader::PSParamBuffer ps_param;
	ps_param.materialsCount = m_materials.size();
	
	
	DMClipMapShader* shader = dynamic_cast<DMClipMapShader*>( GS::System::materials().get( m_shaderName )->m_shader.get() );
	shader->updateMaterials( m_materials );
	shader->SetPSParameters( &ps_param );
	shader->selectPhase( 0 );

	ID3D11ShaderResourceView* height_srv = m_height_map.map();
	DMD3D::instance().GetDeviceContext()->VSSetShaderResources( 0, 1, &height_srv );
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 1, &height_srv );
	ID3D11ShaderResourceView* normal_srv = GS::System::textures().get( m_normalMapName )->srv();
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 1, 1, &normal_srv );

	for( size_t i = 0; i < m_textures.size(); ++i )
	{
		DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 10 + i, 1, &m_textures[i] );
	}
	

	m_clip_map.Render( *shader, camera, frustum );
}

const DMHeightMap& DMTerrain::heightmap()
{
	return m_height_map;
}
