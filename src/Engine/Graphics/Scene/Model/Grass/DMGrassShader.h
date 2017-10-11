#pragma once

#include "..\..\Shaders\DMShader.h"

class DMGrassShader : public DMShader
{
public:
	DMGrassShader();
	~DMGrassShader();

	void StrimOutputDeclaration( D3D11_SO_DECLARATION_ENTRY* );
	void setTextureDistribution( ID3D11ShaderResourceView* );
	void setTextureHeight( ID3D11ShaderResourceView* );
	void setTextureColor( ID3D11ShaderResourceView* );
	void setTerrainColor( ID3D11ShaderResourceView* );
	void setTextureNoise( ID3D11ShaderResourceView* );
	
	void setMapSize( float );

private:	
	void prepareRender(  );
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );
	bool prepare();

private:
	__declspec( align( 16 ) ) struct GrassParam
	{
		float cb_map_size;
	};
	com_unique_ptr<ID3D11Buffer> m_param_buffer;
};

