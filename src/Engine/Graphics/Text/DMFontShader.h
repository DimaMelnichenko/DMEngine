#pragma once

//////////////
// INCLUDES //
//////////////

#include "..\Scene\Shaders\DMShader.h"

class DMFontShader : public DMShader
{
private:	

	struct alignas(16) ParameterType
	{
		D3DXVECTOR4 pixelColor;
	};

public:
	DMFontShader();
	~DMFontShader();
	
	bool setTexure( ID3D11ShaderResourceView* );
	bool setColor( D3DXVECTOR4 color );

private:
	void prepareRender();
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();
	void Update( float );

private:
	com_unique_ptr<ID3D11Buffer> m_param_buffer;
};

