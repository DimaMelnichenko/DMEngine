#pragma once


#include <vector>

#include "DMClipMapShader.h"
#include "DMGeoLevel.h"
#include "..\DMSceneObject.h"
#include "DMClipMapBlock.h"

class DMGeoClipMap : public DMSceneObject
{
public:
	DMGeoClipMap();
	virtual ~DMGeoClipMap();

	bool Initialize( int N, int levels, float scale, float height );

	void Render( DMClipMapShader& shader, const DMCamera&, const DMFrustum& );

	float scaleMultipler();
	float heightMultipler();

private:
	bool InitializeBlocks();

	struct vertex
	{
		D3DXVECTOR3 pos;
	};
	
	DMClipMapBlock m_NxN;
	DMClipMapBlock m_MxM;
	DMClipMapBlock m_Mx3_vert;
	DMClipMapBlock m_Mx3_horiz;
	DMClipMapBlock m_2M1_vert;
	DMClipMapBlock m_2M1_horiz;
	unsigned int m_N;
	unsigned int m_M;

	float m_scale_multipler;
	float m_height_multipler;
	
	std::vector<DMGeoLevel> m_levels;
	std::vector<D3DXVECTOR2> m_MxM_offsets;
	std::vector<D3DXVECTOR2> m_Mx3_offsets;
};

