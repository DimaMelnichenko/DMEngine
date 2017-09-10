#include "DMTerrain.h"

DMTerrain::DMTerrain( DMD3D* parent, std::unique_ptr<DMClipMapShader>& shader ) : DMSceneObject( parent ), m_clip_map_shader( shader )
{
	m_texture_tiles[0] = 1.0f;
	m_texture_tiles[1] = 1.0f;
	m_texture_tiles[2] = 1.0f;
	m_texture_tiles[3] = 1.0f;
	m_texture_tiles[4] = 1.0f;

	m_frustum = std::unique_ptr<DMFrustum>( new DMFrustum() );
}

DMTerrain::~DMTerrain( )
{
	
}

bool DMTerrain::Initialize(  )
{
	

	return 0;
}

bool DMTerrain::loadHeightMap( const std::wstring& file_name, float width_multipler, float height_multipler )
{
	m_height_map = std::unique_ptr<DMHeightMap>( new DMHeightMap( m_dmd3d ) );
	m_height_map->LoadRaw( L"Textures\\terrain\\heightmap.r16", 2049, 2049, 16 );
	//m_height_map->CreateDDSTextureFromFile( file_name );

	int steps = static_cast<int>( max( m_height_map->size().x * width_multipler / 63.0, 2 ) );
	//steps = pow( steps, 0.5 );

	m_clip_map = std::unique_ptr<DMGeoClipMap>( new DMGeoClipMap( m_dmd3d ) );
	//m_clip_map->Initialize( 63, steps + 1, m_height_map->size().x * width_multipler, height_multipler );
	m_clip_map->Initialize( 255, 10, m_height_map->size().x * width_multipler, height_multipler );

	return  true;
}

bool DMTerrain::addBaseTextures( const std::wstring& normal_file_name, const std::wstring& diffuse_file_name )
{
	m_terrain_textures = std::unique_ptr<DMTextureArray>( new DMTextureArray( m_dmd3d ) );
	//m_terrain_textures->Initialize( file_name.data() );
	m_terrain_textures->AddTexture( diffuse_file_name.data() );
	m_terrain_textures->AddTexture( normal_file_name.data() );

	return true;
}

void DMTerrain::setTile( int index, float tile )
{
	if( index < 0 && index > 7 )
		return;

	m_texture_tiles[index] = tile;
}

bool DMTerrain::setTexturesFirstBlock( TextureSet* texture_set )
{	
	m_terrain_textures->AddTexture( texture_set->blend.data() );
	for( size_t i = 0; i < 5; i++ )
	{
		m_terrain_textures->AddTexture( texture_set->albedo[i].data() );
		m_terrain_textures->AddTexture( texture_set->normal[i].data() );
		m_terrain_textures->AddTexture( texture_set->gim[i].data() );
	}

	return true;
}

void DMTerrain::Render( DMCamera* camera, DMFrustum* frustum )
{
	
	static DMClipMapShader::PSParamBuffer ps_param;
	ps_param.base_normal_multipler = 1.1f;
	for( size_t i = 0; i < 8; i++ )
	{
		ps_param.texture_tile[i] = m_texture_tiles[i];
	}

	ps_param.base_normal_multipler = m_clip_map->heightMultipler();
	ps_param.camera_position = camera->position();
	

	m_clip_map_shader->SetPSParameters( &ps_param );

	ID3D11ShaderResourceView* height_srv = m_height_map->map();
	m_dmd3d->GetDeviceContext()->VSSetShaderResources( 0, 1, &height_srv );
	m_clip_map_shader->SetTextures( m_terrain_textures->count(), m_terrain_textures->GetTextureArray() );


	if( !frustum )
	{
		m_frustum->ConstructFrustum( camera, 1000.0 );
		m_clip_map->Render( m_clip_map_shader.get(), camera, m_frustum.get() );
	}
	else
	{
		m_clip_map->Render( m_clip_map_shader.get(), camera, frustum );
	}
	
}

std::unique_ptr<DMHeightMap>& DMTerrain::heightmap()
{
	return m_height_map;
}