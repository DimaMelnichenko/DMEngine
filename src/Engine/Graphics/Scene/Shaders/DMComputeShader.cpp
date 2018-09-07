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

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( ConstantType ), m_constantBuffer ) )
		return false;

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
	if( index < 0 || index > 7 )
		return;

	ID3D11UnorderedAccessView* aUAViews[1] = { resource };
	UINT counters[1] = { 0 };
	DMD3D::instance().GetDeviceContext()->CSSetUnorderedAccessViews( index, 1, aUAViews, counters );
}

void DMComputeShader::Dispatch( uint32_t numElements, float elapsed_time )
{
	DMD3D::instance().GetDeviceContext()->CSSetShader( m_computeShader.get(), nullptr, 0 );

	//////////////////////////////////////
	//	calc

	int group_size_X;
	int group_size_Y;

	int numGroups = ( numElements % 1024 ) ? ( numElements / 1024 + 1 ) : ( numElements / 1024 );
	double secondRoot = pow( (double)numGroups, (double)( 1.0 / 2.0 ) );
	secondRoot = ceilf( secondRoot );
	group_size_X = group_size_Y = (int)secondRoot;

	///////////////////////////////////	
	// set contant

	static ConstantType constantType;
	constantType.groupDim = group_size_X;
	constantType.rect.x = numElements;
	constantType.elapsedTime = elapsed_time;

	setConstants( constantType );

	//////////////////////////////////////
	//////////	DISPATCH	///////////////

	DMD3D::instance().GetDeviceContext()->Dispatch( group_size_X, group_size_Y, 1 );

	clear();
}


void DMComputeShader::Dispatch( uint16_t width, uint16_t height, float elapsed_time )
{
	DMD3D::instance().GetDeviceContext()->CSSetShader( m_computeShader.get(), nullptr, 0 );

	//////////////////////////////////////
	//	calc
	int group_size_X = ( width % 32 != 0 ) ? ( ( width / 32 ) + 1 ) : ( width / 32 );
	int group_size_Y = ( height % 32 != 0 ) ? ( ( height / 32 ) + 1 ) : ( height / 32 );

	///////////////////////////////////	
	// set contant

	static ConstantType constantType;
	constantType.groupDim = group_size_X;
	constantType.rect.x = width;
	constantType.rect.y = height;
	constantType.elapsedTime = elapsed_time;

	setConstants( constantType );

	//////////////////////////////////////
	//////////	DISPATCH /////////////////

	DMD3D::instance().GetDeviceContext()->Dispatch( group_size_X, group_size_Y, 1 );

	clear();
}

void DMComputeShader::setConstants( ConstantType& constantType )
{
	Device::updateResource( m_constantBuffer.get(), constantType );
	
	DMD3D::instance().setConstantBuffer( SRVType::cs, 2, m_constantBuffer );
}

void DMComputeShader::clear()
{
	DMD3D::instance().GetDeviceContext()->CSSetShader( nullptr, nullptr, 0 );

	ID3D11UnorderedAccessView* uav[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	DMD3D::instance().GetDeviceContext()->CSSetUnorderedAccessViews( 0, 8, uav, (UINT*)( &uav ) );

	//ID3D11ShaderResourceView* srv[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	//DMD3D::instance().GetDeviceContext()->CSSetShaderResources( 0, 10, srv );
}