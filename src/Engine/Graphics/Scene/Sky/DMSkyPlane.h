#pragma once

//////////////
// INCLUDES //
//////////////


class DMSkyPlane : public DMSceneObject
{
private:
	struct SkyPlaneType
	{
		float x, y, z;
		float tu, tv;
	};

	struct VertexType
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
	};
public:
	DMSkyPlane( DMD3D* );
	~DMSkyPlane();

	bool Initialize( WCHAR*, WCHAR* );
	void Shutdown( );
	void Render();
	void Frame( );

	int GetIndexCount( );
	com_unique_ptr<ID3D11ShaderResourceView>& GetCloudTexture( );
	com_unique_ptr<ID3D11ShaderResourceView>& GetPerturbTexture( );

	float GetScale( );
	float GetBrightness( );
	float GetTranslation( );

private:
	bool InitializeSkyPlane( int, float, float, float, int );
	void ShutdownSkyPlane( );

	bool InitializeBuffers( int );
	void ShutdownBuffers( );
	void RenderBuffers();

	bool LoadTextures( WCHAR*, WCHAR* );
	void ReleaseTextures( );

private:
	SkyPlaneType* m_skyPlane;
	int m_vertexCount, m_indexCount;
	ID3D11Buffer *m_vertexBuffer, *m_indexBuffer;
	DMTexture *m_CloudTexture, *m_PerturbTexture;
	float m_scale, m_brightness, m_translation;
};

