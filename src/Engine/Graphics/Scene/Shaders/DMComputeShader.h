#pragma once

#include "DirectX.h"
#include <memory>

#include "D3D\DMD3D.h"
#include "DMShader.h"

class DMComputeShader
{
public:
	DMComputeShader();
	~DMComputeShader();

	bool Initialize( const std::string& file_name, const std::string& function_name );
	void setStructuredBuffer( int index, ID3D11ShaderResourceView* );
	void setUAVBuffer( int index, ID3D11UnorderedAccessView* );
	void Dispatch( int num_elements, float elapsed_time );

private:
	void OutputShaderErrorMessage( ID3D10Blob* errorMessage, const std::string& shaderFilename );

	struct alignas( 16 ) ConstantType
	{
		float group_dim;
		float max_particles;
		float elapsed_time;
		float dump;
	};

private:
	com_unique_ptr<ID3D11ComputeShader> m_computeShader;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
};

