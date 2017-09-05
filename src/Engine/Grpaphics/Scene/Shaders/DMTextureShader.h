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

	__declspec( align( 16 ) ) struct PSParamBuffer
	{
		D3DXVECTOR3 camera_position;
		D3DXVECTOR2 tex_tiled;
		float app_time;
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

