#include "DMShader.h"
#include <assert.h>

namespace GS
{

DMShader::DMShader() :
	m_useStrimoutGS( false ),
	m_phaseIdx( 0 )
{
	
}

DMShader::~DMShader()
{
	
}

bool DMShader::initialize( const std::string& vsFilename, const std::string& psFilename, bool use_strimout )
{
	initialize();

	m_useStrimoutGS = use_strimout;

	addShaderPass( vs, "main", vsFilename );
	addShaderPass( ps, "main", psFilename );

	return true;
}

bool DMShader::initialize( const std::string& vsFilename, bool use_strimout )
{
	initialize();

	m_useStrimoutGS = use_strimout;

	addShaderPass( vs, "main", vsFilename );

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

void DMShader::OutputShaderErrorMessage( ID3D10Blob* errorMessage, const std::string& shaderFilename )
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

	// Release the error message.
	errorMessage->Release();
	errorMessage = 0;

	// Pop a message up on the screen to notify the user to check the text file for compile errors.
	MessageBox( 0, "Error compiling shader.  Check shader-error.txt for message.", shaderFilename.data(), MB_OK );

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

bool DMShader::setPass( int phase_idx )
{
	if( !selectPhase( phase_idx ) )
		return false;

	DMShader::Phase phase = m_phases[m_phaseIdx];

	// Set the vertex input layout.
	DMD3D::instance().GetDeviceContext()->IASetInputLayout( m_layout[phase.index_vs].get() );

	// Set the vertex and pixel shaders that will be used to render this triangle.
	ID3D11VertexShader* vs = m_vertexShader[phase.index_vs].get();
	DMD3D::instance().GetDeviceContext()->VSSetShader( vs, NULL, 0 );

	if( m_pixelShader.size() && phase.index_ps >= 0 )
	{
		ID3D11PixelShader* ps = m_pixelShader[phase.index_ps].get();
		DMD3D::instance().GetDeviceContext()->PSSetShader( ps, NULL, 0 );
	}
	else
	{
		DMD3D::instance().GetDeviceContext()->PSSetShader( NULL, NULL, 0 );
	}

	if( m_geometryShader.size() )
	{
		ID3D11GeometryShader* gs = m_geometryShader[phase.index_gs].get();
		DMD3D::instance().GetDeviceContext()->GSSetShader( gs, NULL, 0 );
	}
	else
		DMD3D::instance().GetDeviceContext()->GSSetShader( NULL, NULL, 0 );


	prepare();

	return true;
}

bool DMShader::prepare()
{
	return true;
}

bool DMShader::addShaderPass( SRVType type, 
							  const std::string& function_name,
							  const std::string& file_name, 
							  const std::string& defines )
{

	ID3D10Blob* errorMessage;
	ID3D10Blob* shaderBuffer;
	HRESULT result;
	std::string shader_version( "_5_0" );

	switch( type )
	{
		case SRVType::vs:
			shader_version = "vs" + shader_version;
			break;
		case SRVType::ps:
			shader_version = "ps" + shader_version;
			break;
		case SRVType::gs:
			shader_version = "gs" + shader_version;
			break;
		default:
			break;
	}

	D3D10_SHADER_MACRO* macro = nullptr;

	parseDefines( defines, &macro );

	result = D3DX11CompileFromFile( file_name.data(), macro, NULL, function_name.data(), 
									shader_version.data(), D3D10_SHADER_ENABLE_STRICTNESS, 0, NULL,
									&shaderBuffer, &errorMessage, NULL );

	if( macro )
	{
		delete[] macro;
	}

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
			MessageBox( 0, file_name.data(), "Missing Shader File", MB_OK );
		}

		return false;
	}

	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;

	switch( type )
	{
		case SRVType::vs:
			ID3D11VertexShader* vertex_shader;
			result = DMD3D::instance().GetDevice()->CreateVertexShader( shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), NULL, &vertex_shader );
			if( FAILED( result ) )
			{
				return false;
			}
			m_vertexShader.push_back( make_com_ptr<ID3D11VertexShader>( vertex_shader ) );

			vertex_layout = initLayouts();

			ID3D11InputLayout* layout;
			// Create the vertex input layout.
			if( vertex_layout.size() )
				result = DMD3D::instance().GetDevice()->CreateInputLayout( &vertex_layout[0], vertex_layout.size(), shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), &layout );
			else
			{
				layout = nullptr;
				result = S_OK;
			}

			if( FAILED( result ) )
			{
				return false;
			}

			m_layout.push_back( make_com_ptr<ID3D11InputLayout>( layout ) );

			break;
		case SRVType::ps:
			// Create the pixel shader from the buffer.
			ID3D11PixelShader* pixel_shader;
			result = DMD3D::instance().GetDevice()->CreatePixelShader( shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), NULL, &pixel_shader );
			if( FAILED( result ) )
			{
				return false;
			}
			m_pixelShader.push_back( make_com_ptr<ID3D11PixelShader>( pixel_shader ) );
			break;
		case SRVType::gs:
			ID3D11GeometryShader* geometry_shader;
			if( m_useStrimoutGS )
			{
				D3D11_SO_DECLARATION_ENTRY decl;
				StrimOutputDeclaration( &decl );
				UINT strides = sizeof( XMFLOAT4 );
				result = DMD3D::instance().GetDevice()->CreateGeometryShaderWithStreamOutput( shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), &decl, 1,
																								NULL, 0, 0, 0, &geometry_shader );
			}
			else
			{
				result = DMD3D::instance().GetDevice()->CreateGeometryShader( shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), NULL, &geometry_shader );
			}
			if( FAILED( result ) )
			{
				return false;
			}
			m_geometryShader.push_back( make_com_ptr<ID3D11GeometryShader>( geometry_shader ) );
			break;
		default:
			break;
	}

	shaderBuffer->Release();
	shaderBuffer = 0;

	return true;
}

void DMShader::StrimOutputDeclaration( D3D11_SO_DECLARATION_ENTRY* decl )
{
	decl = nullptr;
	assert( "DMShader::StrimOutputDeclaration - use default, need to reimplement!" );
}

void DMShader::setStreamout( bool use_strimout_gs )
{
	m_useStrimoutGS = use_strimout_gs;
}

void DMShader::createPhase( int index_vs, int index_gs, int index_ps )
{
	int v_size = m_vertexShader.size();
	int g_size = m_geometryShader.size();
	int p_size = m_pixelShader.size();

	if( !( index_vs < v_size &&
			index_gs < g_size &&
			index_ps < p_size ) )
	{
		return;
	}

	Phase new_phase;
	new_phase.index_vs = index_vs;
	new_phase.index_gs = index_gs;
	new_phase.index_ps = index_ps;

	for( auto phase : m_phases )
	{
		if( phase == new_phase )
		{
			return;
		}
	}

	m_phases.push_back( new_phase );
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

void DMShader::parseDefines( std::string defines, D3D10_SHADER_MACRO** macro_result )
{

	if( !defines.size() )
		return;

	std::vector<std::string> comma_split;

	str_split( defines, comma_split, "," );

	D3D10_SHADER_MACRO* macro = new D3D10_SHADER_MACRO[comma_split.size() + 1];

	std::vector<std::string> equal_split;

	for( int i = 0; i < comma_split.size(); ++i )
	{
		str_split( comma_split[i], equal_split, "=" );

		if( equal_split.size() == 2 )
		{
			macro[i].Name = new char[equal_split[0].size() + 1];
			memset( (void*)macro[i].Name, 0, sizeof( char ) * ( equal_split[0].size() + 1 ) );
			memcpy( (void*)macro[i].Name, equal_split[0].data(), sizeof( char ) * equal_split[0].size() );
			macro[i].Definition = new char[equal_split[1].size() + 1];
			memset( (void*)macro[i].Definition, 0, sizeof( char ) * ( equal_split[1].size() + 1 ) );
			memcpy( (void*)macro[i].Definition, equal_split[1].data(), sizeof( char ) * equal_split[1].size() );
		}
		else
		{
			macro[i].Name = nullptr;
			macro[i].Definition = nullptr;
		}
		equal_split.clear();
	}

	macro[comma_split.size()].Name = nullptr;
	macro[comma_split.size()].Definition = nullptr;

	*macro_result = macro;
}

}