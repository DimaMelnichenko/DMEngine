#pragma once

#include "..\Shaders\DMShader.h"

class DMParticleShader : public DMShader
{
public:
	DMParticleShader();
	~DMParticleShader();

	void Update( float );

	void setTexture( ID3D11ShaderResourceView* );
	void setTextureDistributon( ID3D11ShaderResourceView* );
	void setTextureHeight( ID3D11ShaderResourceView* );

	
private:
	void prepareRender();
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void StrimOutputDeclaration( D3D11_SO_DECLARATION_ENTRY* );
};

