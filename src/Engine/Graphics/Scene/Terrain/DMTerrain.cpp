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

bool DMTerrain::Initialize( const std::string& terrainConf, const std::string& shaderName, LibraryLoader& libLoader )
{
	m_shaderName = shaderName;
	if( !libLoader.loadMaterial( 4 ) )
		return false;

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

	m_settings.idHeightmap = jsonParser["heightmap"];
	m_settings.idNormalmap = jsonParser["normalmap"];
	m_settings.heightMultipler = jsonParser["height_multipler"];
	m_settings.widthMultipler = jsonParser["width_multipler"];
	m_settings.wireframeSize = jsonParser["wireframe_size"];
	m_settings.steps = jsonParser["steps"];
	m_settings.mapScale = jsonParser["map_scale"];
	m_settings.mapOffset.x = jsonParser["map_offset"][0];
	m_settings.mapOffset.y = jsonParser["map_offset"][1];
	m_settings.mapOffsetSpeed = jsonParser["map_offset_speed"];

	if( !libLoader.loadTexture( m_settings.idHeightmap ) )
		return false;

	if( !libLoader.loadTexture( m_settings.idNormalmap ) )
		return false;
	
	if( !loadHeightMap() )
		return false;

	m_clip_map.setMapScale( m_settings.mapScale );
	m_clip_map.setMapOffset( m_settings.mapOffset );
	m_clip_map.setMapOffsetSpeed( m_settings.mapOffsetSpeed );

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
		if( !libLoader.loadTexture( jsonMaterial["Texture"] ) )
			return false;
		m_textures.push_back( jsonMaterial["Texture"] );
		m_materials.push_back( material );
	}

	m_isInited = true;

	return true;
}

bool DMTerrain::loadHeightMap()
{
	m_height_map.setHeightMapTexture( GS::System::textures().get( m_settings.idHeightmap ).get() );

	//int steps = static_cast<int>( max( m_height_map.size().x * width_multipler / wireframeSize, 2 ) );
	//steps = (int)pow( steps, 0.5f );
	
	m_clip_map.Initialize( (int)m_settings.wireframeSize, m_settings.steps, m_height_map.size().x, m_settings.widthMultipler, m_settings.heightMultipler );
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
	
	DMD3D::instance().setSRV( SRVType::vs, 0, GS::System::textures().get( m_settings.idHeightmap )->srv() );
	DMD3D::instance().setSRV( SRVType::ps, 0, GS::System::textures().get( m_settings.idHeightmap )->srv() );
	DMD3D::instance().setSRV( SRVType::ps, 1, GS::System::textures().get( m_settings.idNormalmap )->srv() );
	DMD3D::instance().setSRV( SRVType::ps, 2, GS::System::textures().get( 13 )->srv() );

	for( size_t i = 0; i < m_textures.size(); ++i )
	{	
		DMD3D::instance().setSRV( SRVType::ps, 10 + i, GS::System::textures().get( m_textures[i] )->srv() );
	}

	m_clip_map.Render( *shader, camera, frustum );
}

const DMHeightMap& DMTerrain::heightmap()
{
	return m_height_map;
}
