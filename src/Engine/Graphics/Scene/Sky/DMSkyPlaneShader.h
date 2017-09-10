#pragma once

//////////////
// INCLUDES //
//////////////
#include <d3d11.h>
#include <d3dx10math.h>
#include <d3dx11async.h>
#include <fstream>



class DMSkyPlaneShader : public DMObject
{
private:
	struct MatrixBufferType
	{
		D3DXMATRIX world;
		D3DXMATRIX view;
		D3DXMATRIX projection;
	};

	struct SkyBufferType
	{
		float translation;
		float scale;
		float brightness;
		float padding;
	};
public:
	DMSkyPlaneShader(  );
	~DMSkyPlaneShader();

	bool Initialize( ID3D11Device*, HWND );
	void Shutdown( );
	bool Render( ID3D11DeviceContext* deviceContext, int indexCount, D3DXMATRIX worldMatrix, D3DXMATRIX viewMatrix,
				 D3DXMATRIX projectionMatrix, ID3D11ShaderResourceView* cloudTexture, ID3D11ShaderResourceView* perturbTexture,
				 float translation, float scale, float brightness );

private:
	bool InitializeShader( ID3D11Device*, HWND, WCHAR*, WCHAR* );
	void ShutdownShader( );
	void OutputShaderErrorMessage( ID3D10Blob*, HWND, WCHAR* );

	bool SetShaderParameters( ID3D11DeviceContext* deviceContext, D3DXMATRIX worldMatrix, D3DXMATRIX viewMatrix,
							  D3DXMATRIX projectionMatrix, ID3D11ShaderResourceView* cloudTexture,
							  ID3D11ShaderResourceView* perturbTexture, float translation, float scale, float brightness );
	void RenderShader( ID3D11DeviceContext*, int );

private:
	ID3D11VertexShader* m_vertexShader;
	ID3D11PixelShader* m_pixelShader;
	ID3D11InputLayout* m_layout;
	ID3D11SamplerState* m_sampleState;
	ID3D11Buffer* m_matrixBuffer;
	ID3D11Buffer* m_skyBuffer;
};

