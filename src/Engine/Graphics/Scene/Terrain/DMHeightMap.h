#pragma once

#include "DirectX.h"

#include "Utils\utilites.h"
#include "TextureObjects\DMTexture.h"


class DMHeightMap
{

public:
	DMHeightMap();
	~DMHeightMap();

	void setHeightMapTexture( GS::DMTexture* );
	float height( float, float ) const;
	XMFLOAT2 size() const;

private:
	bool in_triangle( XMFLOAT2* d, XMFLOAT2* abc );
	bool checkHeightOfTriangle( XMFLOAT3* point, XMFLOAT3* abc );

	GS::DMTexture* m_heightMap;
};

