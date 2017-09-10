#include "DMComputeShader.h"
#include <fstream>


DMComputeShader::DMComputeShader( DMD3D* parent ) : m_dmd3d( parent )
{
}


DMComputeShader::~DMComputeShader()
{
}

bool DMComputeShader::Initialize( const std::wstring& file_name, const std::string& function_name )
{
	ID3D10Blob* error_message;
	ID3D10Blob* shader_buffer;
	
	HRESULT result = D3DX11CompileFromFile( file_name.data(), NULL, NULL, function_name.data(), "cs_4_0", D3D10_SHADER_ENABLE_STRICTNESS, 0, NULL,
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
			MessageBox( 0, file_name.data(), L"Missing Shader File", MB_OK );
		}

		return false;
	}

	ID3D11ComputeShader* compute_shader;
	result = m_dmd3d->GetDevice()->CreateComputeShader( shader_buffer->GetBufferPointer(), shader_buffer->GetBufferSize(), NULL, &compute_shader );

	shader_buffer->Release();
	shader_buffer = 0;

	if( FAILED( result ) )
	{
		return false;
	}

	m_compute_shader = make_com_ptr<ID3D11ComputeShader>( compute_shader );

	D3D11_BUFFER_DESC desc;
	memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.ByteWidth = sizeof( ConstantType );
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	
	ID3D11Buffer* buffer;
	result = m_dmd3d->GetDevice()->CreateBuffer( &desc, nullptr, &buffer );

	if( FAILED( result ) )
	{
		return false;
	}

	m_constant_buffer = make_com_ptr<ID3D11Buffer>( buffer );

	return true;
}

void DMComputeShader::OutputShaderErrorMessage( ID3D10Blob* errorMessage, const WCHAR* shaderFilename )
{
	char* compileErrors;
	unsigned long bufferSize, i;
	std::ofstream fout;


	// Get a pointer to the error message text buffer.
	compileErrors = (char*)( errorMessage->GetBufferPointer() );

	// Get the length of the message.
	bufferSize = errorMessage->GetBufferSize();

	// Open a file to write the error message to.
	fout.open( "shader-error.txt" );

	// Write out the error message.
	for( i = 0; i<bufferSize; i++ )
	{
		fout << compileErrors[i];
	}

	// Close the file.
	fout.close();

	// Release the error message.
	errorMessage->Release();
	errorMessage = 0;

	// Pop a message up on the screen to notify the user to check the text file for compile errors.
	MessageBox( 0, L"Error compiling shader.  Check shader-error.txt for message.", shaderFilename, MB_OK );

	return;
}

void DMComputeShader::setStructuredBuffer( int index, ID3D11ShaderResourceView* resource )
{
	m_dmd3d->GetDeviceContext()->CSSetShaderResources( index, 1, &resource );
}

void DMComputeShader::setUAVBuffer( int index, ID3D11UnorderedAccessView* resource )
{
	ID3D11UnorderedAccessView* aUAViews[1] = { resource };
	m_dmd3d->GetDeviceContext()->CSSetUnorderedAccessViews( index, 1, aUAViews, (UINT*)(&aUAViews) );
}

void DMComputeShader::Dispatch( int num_elements, float elapsed_time )
{
	m_dmd3d->GetDeviceContext()->CSSetShader( m_compute_shader.get(), nullptr, 0 );

	//////////////////////////////////////
	//	calc

	int group_size_X;
	int group_size_Y;

	int numGroups = ( num_elements % 768 != 0 ) ? ( ( num_elements / 768 ) + 1 ) : ( num_elements / 768 );
	double secondRoot = pow( (double)numGroups, (double)( 1.0 / 2.0 ) );
	secondRoot = ceilf( secondRoot );
	group_size_X = group_size_Y = (int)secondRoot;

	///////////////////////////////////
	// set contant

	D3D11_MAPPED_SUBRESOURCE mapped_resource;

	m_dmd3d->GetDeviceContext()->Map( m_constant_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource );

	ConstantType* data = static_cast<ConstantType*>( mapped_resource.pData );

	data->group_dim = group_size_X;
	data->max_particles = num_elements;
	data->elapsed_time = elapsed_time;

	m_dmd3d->GetDeviceContext()->Unmap( m_constant_buffer.get(), 0 );

	ID3D11Buffer* buff[] = { m_constant_buffer.get() };
	m_dmd3d->GetDeviceContext()->CSSetConstantBuffers( 0, 1, buff );

	//////////////////////////////////////
	//////////	DISPATCH	///////////////

	m_dmd3d->GetDeviceContext()->Dispatch( group_size_X, group_size_Y, 1 );

	//////////////////////////////////////
	//	and clear
	m_dmd3d->GetDeviceContext()->CSSetShader( nullptr, nullptr, 0 );

	ID3D11UnorderedAccessView* uav[] = { nullptr };
	m_dmd3d->GetDeviceContext()->CSSetUnorderedAccessViews( 0, 1, uav, (UINT*)(&uav) );

	ID3D11ShaderResourceView* srv[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	m_dmd3d->GetDeviceContext()->CSSetShaderResources( 0, 10, srv );
}