#pragma once

#include "DirectX.h"
#include <memory>

#include "DMShader.h"

class DMComputeShader
{
public:
	DMComputeShader( DMD3D* );
	~DMComputeShader();

	bool Initialize( const std::wstring& file_name, const std::string& function_name );
	void setStructuredBuffer( int index, ID3D11ShaderResourceView* );
	void setUAVBuffer( int index, ID3D11UnorderedAccessView* );
	void Dispatch( int num_elements, float elapsed_time );

private:
	void OutputShaderErrorMessage( ID3D10Blob* errorMessage, const WCHAR* shaderFilename );

	__declspec( align(16) ) struct ConstantType
	{
		float group_dim;
		float max_particles;
		float elapsed_time;
	};

private:
	DMD3D* m_dmd3d;
	std::shared_ptr<ID3D11ComputeShader> m_compute_shader;
	std::shared_ptr<ID3D11Buffer> m_constant_buffer;
};

