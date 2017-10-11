#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <fstream>
#include "DMShader.h"


class DMTextureShader :	public DMShader
{
public:
	DMTextureShader();
	~DMTextureShader();

	struct alignas(16) PSParamBuffer
	{
		D3DXVECTOR2 tex_tiled;
	};
	
	bool setTexure( ID3D11ShaderResourceView* );
	bool setParameters( PSParamBuffer* );

private:
	void prepareRender(  );
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

private:
	 com_unique_ptr<ID3D11Buffer> m_param_buffer;


};

