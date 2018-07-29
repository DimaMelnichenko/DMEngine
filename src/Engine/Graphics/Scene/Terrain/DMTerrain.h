#pragma once


//////////////
// INCLUDES //
//////////////
#include "DMGeoClipMap.h"
#include "DMHeightMap.h"
#include "ObjectLibrary\LibraryLoader.h"


class DMTerrain
{
public:
	DMTerrain();
	~DMTerrain();

	struct Settings
	{
		float wireframeSize;
		uint8_t steps;
		float heightMultipler;
		float widthMultipler;
		float mapScale;
		XMFLOAT2 mapOffset;
		float mapOffsetSpeed;
		uint64_t idHeightmap;
		uint64_t idNormalmap;
	};
	bool Initialize( const std::string& terrainConf, const std::string& shaderName, LibraryLoader& libLoader );
	void Render( const DMCamera&, const DMFrustum& );
	const DMHeightMap& heightmap();
		  
private:
	DMTerrain( const DMTerrain& );
	bool loadHeightMap();
	
private:
	DMFrustum* m_frustum;
	DMGeoClipMap m_clip_map;
	DMHeightMap m_height_map;
	std::string m_normalMapName;
	float m_widthMultipler;
	float m_hightOffset;
	std::vector<DMClipMapShader::Material> m_materials;
	std::vector<uint64_t> m_textures;
	std::string m_shaderName;
	bool m_isInited;
	Settings m_settings;
};

