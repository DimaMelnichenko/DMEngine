#pragma once

#include "DMFullScreen.h"
#include "DMFullScreenShader.h"
#include "DMRenderTexture.h"

class DMRenderFilter : public DMSceneObject
{
public:
	DMRenderFilter( DMD3D*, DMFullScreen*, DMFullScreenShader* );
	~DMRenderFilter();

	void selectPass( unsigned int );
	void setSRV( unsigned int index, ID3D11ShaderResourceView* );
	void Render( DMCamera*, DMRenderTexture* );
	void clearSRV();

private:
	DMFullScreen* m_full_screen;
	DMFullScreenShader* m_shader;
	unsigned int m_pass;
	std::vector<ID3D11ShaderResourceView*> m_srv;
};

