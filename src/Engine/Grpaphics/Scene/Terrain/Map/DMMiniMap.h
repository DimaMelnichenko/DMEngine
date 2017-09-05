#pragma once

#include "DMSceneObject.h"
#include "dmbitmap.h"
#include "dmtextureshader.h"

class DMMiniMap : public DMSceneObject
{
public:
	DMMiniMap( DMD3D* );
	~DMMiniMap();

	bool Initialize( int, int, D3DXMATRIX, float, float );
	bool Render( DMTextureShader*, D3DXMATRIX*, DMCamera* );

	void PositionUpdate( float, float );

private: 
	int m_mapLocationX, m_mapLocationY, m_pointLocationX, m_pointLocationY;
	float m_mapSizeX, m_mapSizeY, m_terrainWidth, m_terrainHeight;
	D3DXMATRIX m_viewMatrix;

	std::unique_ptr<DMBitmap> m_MiniMapBitmap, m_Border, m_Point;
	std::unique_ptr<DMTexture> m_minimap_texture;
	std::unique_ptr<DMTexture> m_border_texture;
	std::unique_ptr<DMTexture> m_point_texture;
};

