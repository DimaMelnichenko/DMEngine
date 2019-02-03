#include "DMTerrain.h"
#include "System.h"
//#include "json.hpp"
#include <fstream>
#include "Shaders\DMClipMapShader.h"
#include "DBConnector.h"

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

	std::string queryText = "select height_multipler,height_offset,id,map_offset,map_offset_speed,map_scale,name,steps,width_multipler,wireframe_size \
							from GeoClipMap where name = :name";
	SQLite::Statement querySettings( dbConnect().db(), queryText );
	querySettings.bind( ":name", "Terrain" );

	float height_offset = 0.0f;

	while( querySettings.executeStep() )
	{
		m_settings.idHeightmap = GS::System::textures().id( "t_heightmap" );
		m_settings.idNormalmap = GS::System::textures().id( "t_normalmap" );
		m_settings.heightMultipler = querySettings.getColumn( "height_multipler").getDouble();
		m_settings.widthMultipler = querySettings.getColumn( "width_multipler" ).getDouble();
		m_settings.wireframeSize = querySettings.getColumn( "wireframe_size" ).getDouble();
		m_settings.steps = querySettings.getColumn( "steps" ).getInt();
		m_settings.mapScale = querySettings.getColumn( "map_scale" ).getDouble();
		XMFLOAT4 vec;
		strToVec4( querySettings.getColumn( "map_offset" ).getString(), vec );
		m_settings.mapOffset.x = vec.x;
		m_settings.mapOffset.y = vec.y;
		m_settings.mapOffsetSpeed = querySettings.getColumn( "map_offset_speed" ).getDouble();
		height_offset = querySettings.getColumn( "height_offset" ).getDouble();
	}

	if( !libLoader.loadTexture( m_settings.idHeightmap ) )
		return false;

	if( !libLoader.loadTexture( m_settings.idNormalmap ) )
		return false;
	
	if( !loadHeightMap() )
		return false;

	m_clip_map.setMapScale( m_settings.mapScale );
	m_clip_map.setMapOffset( m_settings.mapOffset );
	m_clip_map.setMapOffsetSpeed( m_settings.mapOffsetSpeed );
	
	m_clip_map.setHightOffset( height_offset );


	m_properties.setName( "Old Terrain" );
	auto prop = m_properties.insert( "Height multipler", m_settings.heightMultipler );
	prop->setLow( 1.0 );
	prop->setHigh( 5000.0 );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Map scale", m_settings.mapScale );
	prop->setLow( 0.01 );
	prop->setHigh( 10.0 );
	prop->setControlType( GUIControlType::SLIDER );


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
	
	
	
	DMClipMapShader* shader = dynamic_cast<DMClipMapShader*>( GS::System::materials().get( m_shaderName )->m_shader.get() );
	//shader->updateMaterials( m_materials );
	//shader->SetPSParameters( &ps_param );
	shader->selectPhase( 0 );
	
	DMD3D::instance().setSRV( SRVType::vs, 0, GS::System::textures().get( "t_heightmap" )->srv() );

	DMD3D::instance().setSRV( SRVType::ps, 0, GS::System::textures().get( "t_heightmap" )->srv() );
	DMD3D::instance().setSRV( SRVType::ps, 1, GS::System::textures().get( "t_normalmap" )->srv() ) ;
	DMD3D::instance().setSRV( SRVType::ps, 2, GS::System::textures().get( "t_diffuse" )->srv() );
	DMD3D::instance().setSRV( SRVType::ps, 3, GS::System::textures().get( "monohromeNoise" )->srv() );

	DMD3D::instance().setSRV( SRVType::ps, 10, GS::System::textures().get( "mask_grass" )->srv() );			//Texture2D maskGrass : register( t10 );
	DMD3D::instance().setSRV( SRVType::ps, 11, GS::System::textures().get( "t_grass" )->srv() );			//Texture2D textureGrass : register( t11 );
	DMD3D::instance().setSRV( SRVType::ps, 12, GS::System::textures().get( "mask_rock" )->srv() );			//Texture2D maskRock : register( t12 );
	DMD3D::instance().setSRV( SRVType::ps, 13, GS::System::textures().get( "t_rock" )->srv() );				//Texture2D textureRock : register( t13 );
	DMD3D::instance().setSRV( SRVType::ps, 14, GS::System::textures().get( "mask_boulders" )->srv() );		//Texture2D maskBoulders : register( t14 );
	DMD3D::instance().setSRV( SRVType::ps, 15, GS::System::textures().get( "t_boulders" )->srv() );			//Texture2D textureBoulders : register( t15 );
	DMD3D::instance().setSRV( SRVType::ps, 16, GS::System::textures().get( "mask_snow" )->srv() );			//Texture2D maskSnow : register( t16 );
	DMD3D::instance().setSRV( SRVType::ps, 17, GS::System::textures().get( "t_snow" )->srv() );				//Texture2D textureSnow : register( t17 );
	DMD3D::instance().setSRV( SRVType::ps, 18, GS::System::textures().get( "t_grass_normal" )->srv() );		//Texture2D g_grassNormal : register( t18 );
	DMD3D::instance().setSRV( SRVType::ps, 19, GS::System::textures().get( "t_rock_normal" )->srv() );		//Texture2D g_rockNormal : register( t19 );
	DMD3D::instance().setSRV( SRVType::ps, 20, GS::System::textures().get( "t_boulders_normal" )->srv() );	//Texture2D g_bouldersNormal : register( t20 );
	DMD3D::instance().setSRV( SRVType::ps, 21, GS::System::textures().get( "t_snow_normal" )->srv() );		//Texture2D g_snowNormal : register( t21 );
	DMD3D::instance().setSRV( SRVType::ps, 22, GS::System::textures().get( "t_grass_hmr" )->srv() );		//Texture2D g_grassHMR : register( t22 );
	DMD3D::instance().setSRV( SRVType::ps, 23, GS::System::textures().get( "t_rock_hmr" )->srv() );			//Texture2D g_rockHMR : register( t23 );
	DMD3D::instance().setSRV( SRVType::ps, 24, GS::System::textures().get( "t_boulders_hmr" )->srv() );		//Texture2D g_bouldersHMR : register( t24 );
	DMD3D::instance().setSRV( SRVType::ps, 25, GS::System::textures().get( "t_snow_hmr" )->srv() );			//Texture2D g_snowHMR : register( t25 );

	m_clip_map.setHightMultipler( m_properties["Height multipler"].data<float>() );
	m_clip_map.setMapScale( m_properties["Map scale"].data<float>() );
	m_clip_map.Render( *shader, camera, frustum );
}

const DMHeightMap& DMTerrain::heightmap()
{
	return m_height_map;
}

PropertyContainer* DMTerrain::properties()
{
	return &m_properties;
}