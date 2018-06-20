#pragma once


#include <vector>

#include "Shaders\DMClipMapShader.h"
#include "DMGeoLevel.h"
#include "..\DMSceneObject.h"
#include "DMClipMapBlock.h"

class DMGeoClipMap : public DMSceneObject
{
public:
	DMGeoClipMap();
	virtual ~DMGeoClipMap();

	bool Initialize( int N, uint16_t levels, float mapSize, float widthMultipler, float height );

	void Render( DMClipMapShader& shader, const DMCamera&, const DMFrustum& );

	float widthMultipler();
	float heightMultipler();

	void setHightOffset( float offset );
	void setMapScale( float scale );
	void setMapOffset( float x, float y );
	void setMapOffsetSpeed( float speed );

private:
	bool InitializeBlocks();

	struct vertex
	{
		XMFLOAT3 pos;
	};
	
	DMClipMapBlock m_NxN;
	DMClipMapBlock m_MxM;
	DMClipMapBlock m_Mx3_vert;
	DMClipMapBlock m_Mx3_horiz;
	DMClipMapBlock m_2M1_vert;
	DMClipMapBlock m_2M1_horiz;
	unsigned int m_N;
	unsigned int m_M;

	float m_mapSize;
	float m_widthMultipler;
	float m_height_multipler;
	float m_hightOffset;
	float m_mapScale;
	XMFLOAT2 m_mapOffset;
	float m_mapOffsetSpeed;
	
	std::vector<DMGeoLevel> m_levels;
	std::vector<XMFLOAT2> m_MxM_offsets;
	std::vector<XMFLOAT2> m_Mx3_offsets;
};

