#pragma once

#include "..\..\Shaders\DMTextureShader.h"
#include "..\..\TextureObjects\DMBitmap.h"

class DMMiniMap : public DMSceneObject
{
public:
	DMMiniMap();
	~DMMiniMap();

	bool Initialize( int, int, D3DXMATRIX, float, float );
	bool Render( DMTextureShader*, D3DXMATRIX*, const DMCamera& );

	void PositionUpdate( float, float );

private: 
	int m_mapLocationX, m_mapLocationY, m_pointLocationX, m_pointLocationY;
	float m_mapSizeX, m_mapSizeY, m_terrainWidth, m_terrainHeight;
	D3DXMATRIX m_viewMatrix;

	DMBitmap m_MiniMapBitmap, m_Border, m_Point;
	DMTexture m_minimap_texture;
	DMTexture m_border_texture;
	DMTexture m_point_texture;
};

