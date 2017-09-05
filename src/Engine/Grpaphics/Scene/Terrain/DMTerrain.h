#pragma once


//////////////
// INCLUDES //
//////////////
#include "DMSceneObject.h"
#include "DMGeoClipMap.h"
#include "DMClipMapShader.h"
#include "DMHeightMap.h"
#include "DMTextureArray.h"


class DMTerrain : public DMSceneObject
{
public:
	DMTerrain( DMD3D*, std::unique_ptr<DMClipMapShader>& );
	
	~DMTerrain( );

	bool Initialize( );
	bool loadHeightMap( const std::wstring& height_file_name,  float width_multipler, float height_multipler );
	void Render( DMCamera*, DMFrustum* = nullptr );

	struct TextureSet
	{
		std::wstring blend;
		std::wstring albedo[5];
		std::wstring normal[5];		
		std::wstring gim[5];
	};

	void setTile( int index, float );
		 
	bool setTexturesFirstBlock( TextureSet* );

	bool addBaseTextures( const std::wstring& normal_file_name, const std::wstring& diffuse_file_name );

	std::unique_ptr<DMHeightMap>& heightmap();
		  
private:
	DMTerrain( const DMTerrain& );
	
private:
	std::unique_ptr<DMFrustum> m_frustum;
	std::unique_ptr<DMGeoClipMap> m_clip_map;
	std::unique_ptr<DMClipMapShader>& m_clip_map_shader;
	std::unique_ptr<DMHeightMap> m_height_map;
	std::unique_ptr<DMTextureArray> m_terrain_textures;
	float m_texture_tiles[8];
};

