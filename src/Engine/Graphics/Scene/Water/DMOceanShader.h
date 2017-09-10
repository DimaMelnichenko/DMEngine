#pragma once

class DMOceanShader : public DMShader
{
public:
	DMOceanShader( DMD3D* );
	~DMOceanShader();

	__declspec( align( 16 ) ) struct VSParamBuffer
	{
		D3DXVECTOR2 map_offset;
		float map_scale;
		float map_N;
		float map_height_multippler;
		float map_texture_scale;
		float map_time;
	};

	__declspec( align( 16 ) ) struct PSParamBuffer
	{
		D3DXVECTOR3 camercamera_position;
		float app_time;
	};

	bool SetParameters( VSParamBuffer*, PSParamBuffer* );
	void SetTextures( int count, ID3D11ShaderResourceView** _textures );


private:
	void Shutdown( );
	void prepareRender(  );;
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

private:
	ID3D11Buffer* m_vs_shader_param;
	ID3D11Buffer* m_ps_shader_param;
	ID3D11SamplerState* m_samplerStateColor;
};

