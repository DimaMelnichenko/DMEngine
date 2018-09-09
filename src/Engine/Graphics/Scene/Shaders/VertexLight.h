#pragma once

//////////////
// INCLUDES //
//////////////
#include "DMShader.h"

namespace GS
{

class VertexLight : public DMShader
{
public:


public:
	VertexLight();
	~VertexLight();
	bool Prepare();
	void setParams( const ParamSet& ) override;

private:
	struct PSParam
	{
		XMFLOAT4 tintColor;
		XMFLOAT4 ambientColor;
	};

private:
	bool innerInitialize() override;
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();
	com_unique_ptr<ID3D11Buffer> m_psCB;

};

}