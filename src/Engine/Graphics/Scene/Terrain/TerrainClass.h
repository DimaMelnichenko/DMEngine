#pragma once

#include "..\TextureObjects\DMTexture.h"

/////////////
// GLOBALS //
/////////////
const int TEXTURE_REPEAT = 64;


class TerrainClass : public DM3DObject
{
private:
	struct VertexType
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
		D3DXVECTOR3 normal;
	};

	struct HeightMapType
	{
		float x, y, z;
		float tu, tv;
		float nx, ny, nz;
	};

	struct VectorType
	{
		float x, y, z;
	};

public:
	TerrainClass( DMD3D* );
	~TerrainClass();


	bool Initialize( WCHAR*, WCHAR* );
	void Shutdown( );

	ID3D11ShaderResourceView* GetTexture( );

	int GetVertexCount( );
	void CopyVertexArray( void* );

private:
	bool LoadHeightMap( WCHAR* );
	void NormalizeHeightMap( );
	bool CalculateNormals( );
	void ShutdownHeightMap( );

	void CalculateTextureCoordinates( );
	bool LoadTexture( WCHAR* );	

	bool InitializeBuffers();
	void ShutdownBuffers( );

private:
	int m_terrainWidth, m_terrainHeight;
	HeightMapType* m_heightMap;
	std::unique_ptr<DMTexture> m_Texture;
	int m_vertexCount;
	VertexType* m_vertices;

};

