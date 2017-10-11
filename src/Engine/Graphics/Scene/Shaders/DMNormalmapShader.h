#pragma once
#include "DMShader.h"

class DMNormalmapShader : public DMShader
{
public:
	DMNormalmapShader();
	~DMNormalmapShader();

	__declspec( align( 16 ) ) struct PSParamBuffer
	{
		D3DXVECTOR3 camera_position;
		D3DXVECTOR2 tex_tiled;
		float app_time;
	};

	bool setTexure( ID3D11ShaderResourceView** );
	bool setParameters( PSParamBuffer* );

private:
	void prepareRender(  );;
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

private:
	ID3D11SamplerState* m_sampleState;
	ID3D11Buffer* m_param_buffer;
};

