#include "DMComputeShader.h"
#include <fstream>


DMComputeShader::DMComputeShader()
{
}


DMComputeShader::~DMComputeShader()
{
}

bool DMComputeShader::Initialize( const std::string& file_name, const std::string& function_name )
{
	ID3D10Blob* error_message;
	ID3D10Blob* shader_buffer;
	
	HRESULT result = D3DX11CompileFromFile( file_name.data(), NULL, NULL, function_name.data(), "cs_5_0", D3D10_SHADER_ENABLE_STRICTNESS, 0, NULL,
									&shader_buffer, &error_message, NULL );

	if( FAILED( result ) )
	{
		// If the shader failed to compile it should have writen something to the error message.
		if( error_message )
		{
			OutputShaderErrorMessage( error_message, file_name.data() );
		}
		// If there was nothing in the error message then it simply could not find the shader file itself.
		else
		{
			MessageBox( 0, file_name.data(), "Missing Shader File", MB_OK );
		}

		return false;
	}

	ID3D11ComputeShader* compute_shader;
	result = DMD3D::instance().GetDevice()->CreateComputeShader( shader_buffer->GetBufferPointer(), shader_buffer->GetBufferSize(), NULL, &compute_shader );

	shader_buffer->Release();
	shader_buffer = 0;

	if( FAILED( result ) )
	{
		return false;
	}

	m_computeShader = make_com_ptr<ID3D11ComputeShader>( compute_shader );

	DMD3D::instance().createShaderConstantBuffer( sizeof( ConstantType ), m_constantBuffer );

	return true;
}

void DMComputeShader::OutputShaderErrorMessage( ID3D10Blob* errorMessage, const std::string& shaderFilename )
{
	// Get a pointer to the error message text buffer.
	char* compileErrors = (char*)( errorMessage->GetBufferPointer() );

	// Get the length of the message.
	unsigned long bufferSize = errorMessage->GetBufferSize();

	// Open a file to write the error message to.
	std::ofstream fout;
	fout.open( "shader-error.txt" );

	// Write out the error message.
	for( size_t i = 0; i < bufferSize; i++ )
	{
		fout << compileErrors[i];
	}

	// Close the file.
	fout.close();

	// Release the error message.
	errorMessage->Release();
	errorMessage = 0;

	// Pop a message up on the screen to notify the user to check the text file for compile errors.
	MessageBox( 0, "Error compiling shader.  Check shader-error.txt for message.", shaderFilename.data(), MB_OK );

	return;
}

void DMComputeShader::setStructuredBuffer( int index, ID3D11ShaderResourceView* resource )
{
	DMD3D::instance().GetDeviceContext()->CSSetShaderResources( index, 1, &resource );
}

void DMComputeShader::setUAVBuffer( int index, ID3D11UnorderedAccessView* resource )
{
	ID3D11UnorderedAccessView* aUAViews[1] = { resource };
	DMD3D::instance().GetDeviceContext()->CSSetUnorderedAccessViews( index, 1, aUAViews, (UINT*)(&aUAViews) );
}

void DMComputeShader::Dispatch( int num_elements, float elapsed_time )
{
	DMD3D::instance().GetDeviceContext()->CSSetShader( m_computeShader.get(), nullptr, 0 );

	//////////////////////////////////////
	//	calc

	int group_size_X;
	int group_size_Y;

	int numGroups = ( num_elements % 1024 != 0 ) ? ( ( num_elements / 1024 ) + 1 ) : ( num_elements / 1024 );
	double secondRoot = pow( (double)numGroups, (double)( 1.0 / 2.0 ) );
	secondRoot = ceilf( secondRoot );
	group_size_X = group_size_Y = (int)secondRoot;

	///////////////////////////////////	
	// set contant

	static ConstantType constantType;
	constantType.group_dim = group_size_X;
	constantType.max_particles = num_elements;
	constantType.elapsed_time = elapsed_time;


	Device::updateResource( m_constantBuffer.get(), constantType );

	ID3D11Buffer* buff[] = { m_constantBuffer.get() };
	DMD3D::instance().GetDeviceContext()->CSSetConstantBuffers( 4, 1, buff );

	//////////////////////////////////////
	//////////	DISPATCH	///////////////

	DMD3D::instance().GetDeviceContext()->Dispatch( group_size_X, group_size_Y, 1 );

	//////////////////////////////////////
	//	and clear
	DMD3D::instance().GetDeviceContext()->CSSetShader( nullptr, nullptr, 0 );

	ID3D11UnorderedAccessView* uav[] = { nullptr };
	DMD3D::instance().GetDeviceContext()->CSSetUnorderedAccessViews( 0, 1, uav, (UINT*)(&uav) );

	ID3D11ShaderResourceView* srv[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	DMD3D::instance().GetDeviceContext()->CSSetShaderResources( 0, 10, srv );
}