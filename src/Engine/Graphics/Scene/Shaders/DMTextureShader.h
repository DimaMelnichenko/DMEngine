#pragma once

//////////////
// INCLUDES //
//////////////
#include <d3d11.h>
#include <d3dx10math.h>
#include <d3dx11async.h>
#include <fstream>
#include "DMShader.h"


class DMTextureShader :	public DMShader
{
public:
	DMTextureShader( DMD3D* );
	~DMTextureShader();

	struct alignas(16) PSParamBuffer
	{
		D3DXVECTOR2 tex_tiled;
	};
	
	bool setTexure( ID3D11ShaderResourceView* );
	bool setParameters( PSParamBuffer* );

private:
	void Shutdown( );
	void prepareRender(  );
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

private:
	 com_unique_ptr<ID3D11Buffer> m_param_buffer;


};

