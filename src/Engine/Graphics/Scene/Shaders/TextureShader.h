#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <fstream>
#include "DMShader.h"

namespace GS
{

class TextureShader : public DMShader
{
public:
	TextureShader();
	~TextureShader();

	bool Prepare();
	void setParams( const ParamSet& ) override;

private:
	bool innerInitialize() override;
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;


};

}
