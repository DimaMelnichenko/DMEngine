#pragma once


//////////////
// INCLUDES //
//////////////
#include "DMGeoClipMap.h"
#include "DMHeightMap.h"


class DMTerrain
{
public:
	DMTerrain();
	~DMTerrain();

	bool Initialize( const std::string& terrainConf );
	void Render( const DMCamera&, const DMFrustum& );
	const DMHeightMap& heightmap();
		  
private:
	DMTerrain( const DMTerrain& );
	bool loadHeightMap( const std::string& height_file_name, float width_multipler, float height_multipler, float wireframeSize, int steps );
	
private:
	DMFrustum m_frustum;
	DMGeoClipMap m_clip_map;
	DMHeightMap m_height_map;
	std::vector<std::string> m_textures;
	std::vector<float> m_texture_tiles;
	std::string m_normalMapName;
	float m_widthMultipler;
	float m_hightOffset;
};

