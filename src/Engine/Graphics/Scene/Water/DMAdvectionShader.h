#pragma once



class DMAdvectionShader : public DMShader
{
public:

	__declspec( align( 32 ) ) struct PSParamBuffer
	{
		float elapsed_time;		
	};

public:
	DMAdvectionShader( DMD3D* );
	~DMAdvectionShader();

	void Shutdown();

	void setParameters( PSParamBuffer* );
	void setTexures( int, ID3D11ShaderResourceView** );

private:
	virtual void prepareRender();
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

private:
	std::shared_ptr<ID3D11SamplerState> m_texture_sampler;
	std::shared_ptr<ID3D11Buffer> m_param_buffer;

};

