#pragma once


//////////////
// INCLUDES //
//////////////
#include "..\DMSceneObject.h"
#include "DMGeoClipMap.h"
#include "DMClipMapShader.h"
#include "DMHeightMap.h"
#include "..\TextureObjects\DMTextureArray.h"


class DMTerrain : public DMSceneObject
{
private:
	static const int m_numTexturesinPack = 5;

public:
	DMTerrain();
	~DMTerrain( );

	bool Initialize( DMClipMapShader* );
	bool loadHeightMap( const std::wstring& height_file_name,  float width_multipler, float height_multipler );
	void Render( const DMCamera&, const DMFrustum& );

	struct TextureSet
	{
		std::wstring blend;
		std::wstring albedo[m_numTexturesinPack];
		std::wstring normal[m_numTexturesinPack];
	};

	void setTile( int index, float );
		 
	bool setTexturesFirstBlock( TextureSet* );

	bool addBaseTextures( const std::wstring& normal_file_name, const std::wstring& diffuse_file_name );

	const DMHeightMap& heightmap();
		  
private:
	DMTerrain( const DMTerrain& );
	
private:
	DMClipMapShader* m_clip_map_shader;

	DMFrustum m_frustum;
	DMGeoClipMap m_clip_map;
	DMHeightMap m_height_map;
	DMTextureArray m_terrain_textures;
	float m_texture_tiles[8];
	
};

