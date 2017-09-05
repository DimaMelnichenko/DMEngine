#pragma once

//////////////
// INCLUDES //
//////////////
#include "DMSceneObject.h"
#include "DMTexture.h"
#include "DMTransformBuffer.h"


class DMWater : public DMSceneObject
{
private:
	struct VertexType
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
	};

public:
	DMWater( DMD3D* );
	~DMWater();

	bool Initialize( WCHAR*, float, float );
	void Shutdown( );
	void Frame( );
	void Render();

	int GetIndexCount( );
	com_unique_ptr<ID3D11ShaderResourceView>& GetTexture( );

	float GetWaterHeight( );
	D3DXVECTOR2 GetNormalMapTiling( );
	float GetWaterTranslation( );
	float GetReflectRefractScale( );
	D3DXVECTOR4 GetRefractionTint( );
	float GetSpecularShininess( );

private:
	bool InitializeBuffers( float );
	void ShutdownBuffers( );
	void RenderBuffers();

	bool LoadTexture( WCHAR* );
	void ReleaseTexture( );

private:
	float m_waterHeight;
	std::shared_ptr<ID3D11Buffer> m_vertexBuffer, m_indexBuffer;
	int m_vertexCount, m_indexCount;
	std::unique_ptr<DMTexture> m_Texture;
	D3DXVECTOR2 m_normalMapTiling;
	float m_waterTranslation;
	float m_reflectRefractScale;
	D3DXVECTOR4 m_refractionTint;
	float m_specularShininess;
};

