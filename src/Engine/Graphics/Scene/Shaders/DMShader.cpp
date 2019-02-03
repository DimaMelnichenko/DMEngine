#include "DMShader.h"
#include <assert.h>
#include "Logger\Logger.h"
#include <d3dcompiler.h>
#include "ShaderUtils.h"

#define RETFALSE_IF_FAILED(x) \
{ \
	if(FAILED(x)) \
	{ \
		return false; \
	} \
} \

namespace GS
{

DMShader::DMShader() :
	m_phaseIdx( 0 )
{
	
}

DMShader::~DMShader()
{
	
}

bool DMShader::initialize( const std::string& vsFilename, const std::string& psFilename, bool use_strimout )
{
	initialize();

	addShaderPassFromFile( vs, "main", vsFilename );
	addShaderPassFromFile( ps, "main", psFilename );

	return true;
}

bool DMShader::initialize( const std::string& vsFilename, bool use_strimout )
{
	initialize();

	addShaderPassFromFile( vs, "main", vsFilename );

	return true;
}

bool DMShader::initialize()
{
	return innerInitialize();
}

bool DMShader::render( int indexCount, uint32_t vertexOffset, uint32_t indexOffset )
{
	RenderShader( indexCount, vertexOffset, indexOffset );

	return true;
}

bool DMShader::renderInstanced( int indexCount, uint32_t vertexOffset, uint32_t indexOffset, int instance_count )
{
	DrawType prev = m_drawType;
	m_drawType = by_index_instance;

	RenderShader( indexCount, vertexOffset, indexOffset, instance_count );

	m_drawType = prev;

	return true;
}

void DMShader::OutputShaderErrorMessage( com_unique_ptr<ID3DBlob>& errorMessage, const std::string& shaderFilename )
{
	char* compileErrors;
	unsigned long bufferSize, i;
	std::wofstream fout;


	// Get a pointer to the error message text buffer.
	compileErrors = (char*)( errorMessage->GetBufferPointer() );

	// Get the length of the message.
	bufferSize = errorMessage->GetBufferSize();

	// Open a file to write the error message to.
	fout.open( "shader-error.txt" );

	fout << "file :" << shaderFilename.data() << std::endl;

	// Write out the error message.
	for( i = 0; i < bufferSize; i++ )
	{
		fout << compileErrors[i];
	}

	// Close the file.
	fout.close();

	// Pop a message up on the screen to notify the user to check the text file for compile errors.
	LOG( "Error compiling shader. " + shaderFilename );

	return;
}

void DMShader::RenderShader( int indexCount, uint32_t vertexOffset, uint32_t indexOffset, int instance_count )
{
	// Render the triangle.
	switch( m_drawType )
	{
		case by_vertex:
			DMD3D::instance().GetDeviceContext()->Draw( indexCount, vertexOffset );
			break;
		case by_index:
			DMD3D::instance().GetDeviceContext()->DrawIndexed( indexCount, indexOffset, vertexOffset );
			break;
		case by_index_instance:
			DMD3D::instance().GetDeviceContext()->DrawIndexedInstanced( indexCount, instance_count, indexOffset, vertexOffset, 0 );
			break;
		case by_auto:
			DMD3D::instance().GetDeviceContext()->DrawAuto();
			break;
		default:
			break;
	}

	return;
}

void DMShader::renderInstancedIndirect( ID3D11Buffer* args )
{
	DMD3D::instance().GetDeviceContext()->DrawIndexedInstancedIndirect( args, 0 );
}

bool DMShader::setPass( int phase_idx )
{
	if( !selectPhase( phase_idx ) )
		return false;

	DMShader::Phase phase = m_phases[m_phaseIdx];

	// Set the vertex input layout.
	DMD3D::instance().GetDeviceContext()->IASetInputLayout( m_layout.get() );

	// Set the vertex and pixel shaders that will be used to render this triangle.
	ID3D11VertexShader* vs = m_vertexShader[phase.index_vs].get();
	DMD3D::instance().GetDeviceContext()->VSSetShader( vs, nullptr, 0 );

	if( m_geometryShader.size() )
	{
		ID3D11GeometryShader* shader = m_geometryShader[phase.index_gs].get();
		DMD3D::instance().GetDeviceContext()->GSSetShader( shader, nullptr, 0 );
	}
	else
		DMD3D::instance().GetDeviceContext()->GSSetShader( nullptr, nullptr, 0 );

	if( m_hullShader.size() && phase.index_hs >= 0 )
	{
		ID3D11HullShader* shader = m_hullShader[phase.index_hs].get();
		DMD3D::instance().GetDeviceContext()->HSSetShader( shader, nullptr, 0 );
	}
	else
	{
		DMD3D::instance().GetDeviceContext()->HSSetShader( nullptr, nullptr, 0 );
	}

	if( m_domainShader.size() && phase.index_ds >= 0 )
	{
		ID3D11DomainShader* shader = m_domainShader[phase.index_ds].get();
		DMD3D::instance().GetDeviceContext()->DSSetShader( shader, nullptr, 0 );
	}
	else
	{
		DMD3D::instance().GetDeviceContext()->DSSetShader( nullptr, nullptr, 0 );
	}

	if( m_pixelShader.size() && phase.index_ps >= 0 )
	{
		ID3D11PixelShader* ps = m_pixelShader[phase.index_ps].get();
		DMD3D::instance().GetDeviceContext()->PSSetShader( ps, nullptr, 0 );
	}
	else
	{
		DMD3D::instance().GetDeviceContext()->PSSetShader( nullptr, nullptr, 0 );
	}

	prepare();

	return true;
}

bool DMShader::prepare()
{
	return true;
}

std::string DMShader::version( SRVType type )
{
	std::string shaderVersion( "_5_0" );

	switch( type )
	{
		case SRVType::vs:
			shaderVersion = "vs" + shaderVersion;
			break;
		case SRVType::ps:
			shaderVersion = "ps" + shaderVersion;
			break;
		case SRVType::gs:
			shaderVersion = "gs" + shaderVersion;
			break;
		case SRVType::hs:
			shaderVersion = "hs" + shaderVersion;
			break;
		case SRVType::ds:
			shaderVersion = "ds" + shaderVersion;
			break;
		default:
			break;
	}

	return shaderVersion;
}

void DMShader::setLayoutDesc( std::vector<D3D11_INPUT_ELEMENT_DESC>&& layoutDesc )
{
	m_layoutDesc = std::move( layoutDesc );
}

bool DMShader::addShaderPassFromMem( SRVType type, const std::string& funcName, const std::string& shaderCode, const std::string& defines )
{	
	std::vector<D3D_SHADER_MACRO> macros;
	parseDefines( defines, macros );

	ID3DBlob* buffer = nullptr;
	ID3DBlob* error = nullptr;
	HRESULT result = D3DCompile( shaderCode.data(), sizeof( std::string::value_type ) * shaderCode.size(), nullptr, 
								 macros.empty() ? nullptr : &macros[0], 
								 D3D_COMPILE_STANDARD_FILE_INCLUDE, funcName.data(),
								 version( type ).data(), D3D10_SHADER_ENABLE_STRICTNESS, 0, &buffer, &error );

	com_unique_ptr<ID3DBlob> errorMessage( error );
	com_unique_ptr<ID3DBlob> shaderBuffer( buffer );

	if( FAILED( result ) )
	{
		// If the shader failed to compile it should have writen something to the error message.
		if( errorMessage )
		{
			OutputShaderErrorMessage( errorMessage, "shaderCode" );
		}
		// If there was nothing in the error message then it simply could not find the shader file itself.
		else
		{
			LOG( std::string("Missing Shader File: ") + "shaderCode" );
		}

		return false;
	}

	return createShaderPass( type, shaderBuffer );
}

bool DMShader::addShaderPassFromFile( SRVType type,
									  const std::string& function_name,
									  const std::string& file_name,
									  const std::string& defines )
{
	std::vector<D3D_SHADER_MACRO> macros;

	parseDefines( defines, macros );

	std::wstring fileName( file_name.begin(), file_name.end() );

	ID3DBlob* buffer = nullptr;
	ID3DBlob* error = nullptr;
	HRESULT result = D3DCompileFromFile( fileName.data(), 
										 macros.empty() ? nullptr : &macros[0], 
										 D3D_COMPILE_STANDARD_FILE_INCLUDE, function_name.data(),
										 version( type ).data(), 
										 D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_PREFER_FLOW_CONTROL,
										 0, &buffer, &error );
	com_unique_ptr<ID3DBlob> errorMessage( error );
	com_unique_ptr<ID3DBlob> shaderBuffer( buffer );

	if( FAILED( result ) )
	{
		// If the shader failed to compile it should have writen something to the error message.
		if( errorMessage )
		{
			OutputShaderErrorMessage( errorMessage, file_name );
		}
		// If there was nothing in the error message then it simply could not find the shader file itself.
		else
		{
			LOG( "Missing Shader File: " + file_name );
		}

		return false;
	}

	return createShaderPass( type, shaderBuffer );
}

bool DMShader::createShaderPass( SRVType type, com_unique_ptr<ID3DBlob>& shaderBuffer )
{
	HRESULT result = S_OK;

	switch( type )
	{
		case SRVType::vs:
		{
			com_unique_ptr<ID3D11VertexShader> vertexShader;

			RETFALSE_IF_FAILED( createShader( shaderBuffer, vertexShader ) );

			m_vertexShader.push_back( std::move( vertexShader ) );

			result = S_OK;
			if( m_layoutDesc.size() )
			{
				RETFALSE_IF_FAILED( createInputLayout( m_layoutDesc, shaderBuffer, m_layout ) );
			}
			
			break;
		}
		case SRVType::ps:
		{
			com_unique_ptr<ID3D11PixelShader> pixelShader;
			RETFALSE_IF_FAILED( createShader( shaderBuffer, pixelShader ) );
			m_pixelShader.push_back( std::move( pixelShader ) );
			break;
		}
		case SRVType::gs:
		{
			com_unique_ptr<ID3D11GeometryShader> geometryShader;
			RETFALSE_IF_FAILED( createShader( shaderBuffer, geometryShader ) );
			m_geometryShader.push_back( std::move( geometryShader ) );
			break;
		}
		case SRVType::hs:
		{
			com_unique_ptr<ID3D11HullShader> hullShader;
			RETFALSE_IF_FAILED( createShader( shaderBuffer, hullShader ) );
			m_hullShader.push_back( std::move( hullShader ) );
			break;
		}
		case SRVType::ds:
		{
			com_unique_ptr<ID3D11DomainShader> domainShader;
			RETFALSE_IF_FAILED( createShader( shaderBuffer, domainShader ) );
			m_domainShader.push_back( std::move( domainShader ) );
			break;
		}
		default:
			break;
	}

	return true;
}

bool DMShader::createPhase( int index_vs, int index_ps, int index_gs, int index_hs, int index_ds )
{
	int v_size = m_vertexShader.size();
	int g_size = m_geometryShader.size();
	int p_size = m_pixelShader.size();
	int h_size = m_hullShader.size();
	int d_size = m_domainShader.size();

	if( !( index_vs < v_size &&
			index_gs < g_size &&
			index_ps < p_size &&
			index_hs < h_size &&
			index_ds < d_size ) )
	{
		return false;
	}

	Phase new_phase;
	new_phase.index_vs = index_vs;
	new_phase.index_gs = index_gs;
	new_phase.index_ps = index_ps;
	new_phase.index_hs = index_hs;
	new_phase.index_ds = index_ds;

	for( auto phase : m_phases )
	{
		if( phase == new_phase )
		{
			return true;
		}
	}

	m_phases.push_back( new_phase );

	return true;
}

bool DMShader::selectPhase( unsigned int idx )
{
	if( idx < m_phases.size() )
	{
		m_phaseIdx = idx;
		return true;
	}

	return false;
}

void DMShader::setDrawType( DrawType type )
{
	m_drawType = type;
}

int DMShader::phase()
{
	return m_phaseIdx;
}

void DMShader::parseDefines( std::string defines, std::vector<D3D_SHADER_MACRO>& macros )
{
	if( !defines.size() )
		return;

	std::vector<std::string> comma_split;

	str_split( defines, comma_split, "," );

	macros.reserve( comma_split.size() + 1 );

	std::vector<std::string> equal_split;

	D3D_SHADER_MACRO macrosItem;
	for( int i = 0; i < comma_split.size(); ++i )
	{
		str_split( comma_split[i], equal_split, "=" );

		if( equal_split.size() == 2 )
		{
			
			macrosItem.Name = new char[equal_split[0].size() + 1];
			memset( (void*)macrosItem.Name, 0, sizeof( char ) * ( equal_split[0].size() + 1 ) );
			memcpy( (void*)macrosItem.Name, equal_split[0].data(), sizeof( char ) * equal_split[0].size() );
			macrosItem.Definition = new char[equal_split[1].size() + 1];
			memset( (void*)macrosItem.Definition, 0, sizeof( char ) * ( equal_split[1].size() + 1 ) );
			memcpy( (void*)macrosItem.Definition, equal_split[1].data(), sizeof( char ) * equal_split[1].size() );
		}
		else
		{
			macrosItem.Name = nullptr;
			macrosItem.Definition = nullptr;
		}
		equal_split.clear();

		macros.push_back( macrosItem );
	}

	macrosItem.Name = nullptr;
	macrosItem.Definition = nullptr;
	macros.push_back( macrosItem );
}

void DMShader::setParams( const PropertyContainer& )
{
	
}

bool DMShader::innerInitialize()
{
	return true;
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMShader::initLayouts()
{
	return std::vector<D3D11_INPUT_ELEMENT_DESC>();
}

}