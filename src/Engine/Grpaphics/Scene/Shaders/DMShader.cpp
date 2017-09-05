#include "DMShader.h"
#include <assert.h>




DMShader::DMShader( DMD3D* _parent ) : m_dmd3d( _parent )
{	
	m_use_strimout_gs = false;
	m_phase_idx = 0;
}

DMShader::~DMShader()
{
	Shutdown();
}

bool DMShader::Initialize( WCHAR* vsFilename, WCHAR* psFilename, bool use_strimout )
{
	Initialize();

	m_use_strimout_gs = use_strimout;

	addShaderPass( vs, "main", vsFilename );
	addShaderPass( ps, "main", psFilename );

	return true;
}

bool DMShader::Initialize( WCHAR* vsFilename, bool use_strimout )
{
	Initialize();

	m_use_strimout_gs = use_strimout;

	addShaderPass( vs, "main", vsFilename );

	return true;
}

bool DMShader::Initialize()
{
	return initialize();
}

bool DMShader::initialize( )
{
	m_dmd3d->createShaderConstantBuffer( sizeof( WorldBuffer ), m_world_buffer );

	m_dmd3d->createShaderConstantBuffer( sizeof( VPCameraBuffer ), m_vpcamera_buffer );

	m_dmd3d->createShaderConstantBuffer( sizeof( PSApplication ), m_ps_application_buffer );

	m_timer = new DMTimer( );
	m_timer->Initialize();

	return true;
}

void DMShader::setWorldMatrix( D3DXMATRIX* worldMatrix )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	D3DXMATRIX mat;
	
	if( !worldMatrix )
	{
		D3DXMatrixIdentity( &mat );
		D3DXMatrixTranspose( &mat, &mat );
	}
	else
	{
		D3DXMatrixTranspose( &mat, worldMatrix );
	}

	// Lock the constant buffer so it can be written to.
	HRESULT result = m_dmd3d->GetDeviceContext()->Map( m_world_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	// Get a pointer to the data in the constant buffer.
	WorldBuffer* dataPtr = (WorldBuffer*)mappedResource.pData;

	// Copy the matrices into the constant buffer.
	memcpy( &dataPtr->world, &mat, sizeof( D3DXMATRIX ) );	

	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( m_world_buffer.get(), 0 );

	ID3D11Buffer* buffer = m_world_buffer.get();
	m_dmd3d->GetDeviceContext()->VSSetConstantBuffers( 1, 1, &buffer );
}

void DMShader::setCamera( DMCamera* camera )
{
	HRESULT result;
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	VPCameraBuffer* dataPtr;
	unsigned int bufferNumber;

	D3DXMATRIX viewMatrix;
	D3DXMATRIX projectionMatrix;

	camera->viewMatrix( &viewMatrix );
	camera->projectionMatrix( &projectionMatrix );

	// Transpose the matrices to prepare them for the shader.
	
	D3DXMatrixTranspose( &viewMatrix, &viewMatrix );
	D3DXMatrixTranspose( &projectionMatrix, &projectionMatrix );

	// Lock the constant buffer so it can be written to.
	result = m_dmd3d->GetDeviceContext()->Map( m_vpcamera_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	// Get a pointer to the data in the constant buffer.
	dataPtr = (VPCameraBuffer*)mappedResource.pData;

	// Copy the matrices into the constant buffer.
	memcpy( &dataPtr->view, &viewMatrix, sizeof(D3DXMATRIX) );
	memcpy( &dataPtr->projection, &projectionMatrix, sizeof( D3DXMATRIX ) );
	camera->position( &dataPtr->camera_position );
	camera->viewDirection( &dataPtr->view_direction );

	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( m_vpcamera_buffer.get(), 0 );

	// Set the position of the constant buffer in the vertex shader.
	bufferNumber = 0;

	// Now set the constant buffer in the vertex shader with the updated values.
	ID3D11Buffer* buffer = m_vpcamera_buffer.get();
	m_dmd3d->GetDeviceContext()->VSSetConstantBuffers( 0, 1, &buffer );

	if( m_geometry_shader.size() )
	{
		m_dmd3d->GetDeviceContext()->GSSetConstantBuffers( 0, 1, &buffer );
	}

	PSApplication* ps_data = nullptr;

	result = m_dmd3d->GetDeviceContext()->Map( m_ps_application_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	ps_data = (PSApplication*)mappedResource.pData;

	m_timer->Frame();

	ps_data->app_time = static_cast<float>( m_timer->totalTime() );
	ps_data->elapsed_time = static_cast<float>( m_timer->GetTime() );
	camera->position( &ps_data->camera_position );

	m_dmd3d->GetDeviceContext()->Unmap( m_ps_application_buffer.get(), 0 );

	buffer = m_ps_application_buffer.get();
	m_dmd3d->GetDeviceContext()->PSSetConstantBuffers( 10, 1, &buffer );

	return;
}

bool DMShader::Render( int indexCount, D3DXMATRIX* worldMatrix )
{
	setWorldMatrix( worldMatrix );

	prepareRender();

	RenderShader( indexCount );

	return true;
}

bool DMShader::RenderInstanced( int indexCount, int instance_count, D3DXMATRIX* worldMatrix )
{
	DrawType prev = m_draw_type;
	m_draw_type = by_index_instance;

	setWorldMatrix( worldMatrix );

	prepareRender( );

	RenderShader( indexCount, instance_count );

	m_draw_type = prev;

	return true;
}

void DMShader::Shutdown()
{
	
}

void DMShader::OutputShaderErrorMessage( ID3D10Blob* errorMessage, const WCHAR* shaderFilename )
{
	char* compileErrors;
	unsigned long bufferSize, i;
	std::wofstream fout;


	// Get a pointer to the error message text buffer.
	compileErrors = (char*)( errorMessage->GetBufferPointer( ) );

	// Get the length of the message.
	bufferSize = errorMessage->GetBufferSize( );

	// Open a file to write the error message to.
	fout.open( "shader-error.txt" );

	fout << "file :" << shaderFilename << std::endl;

	// Write out the error message.
	for( i = 0; i<bufferSize; i++ )
	{
		fout << compileErrors[i];
	}

	// Close the file.
	fout.close( );

	// Release the error message.
	errorMessage->Release( );
	errorMessage = 0;

	// Pop a message up on the screen to notify the user to check the text file for compile errors.
	MessageBox( 0, L"Error compiling shader.  Check shader-error.txt for message.", shaderFilename, MB_OK );

	return;
}

void DMShader::RenderShader( int indexCount, int instance_count )
{
	// Render the triangle.
	switch( m_draw_type )
	{
		case by_vertex:			
			m_dmd3d->GetDeviceContext()->Draw( indexCount, 0 );
			break;
		case by_index:
			m_dmd3d->GetDeviceContext()->DrawIndexed( indexCount, 0, 0 );
			break;
		case by_index_instance:
			m_dmd3d->GetDeviceContext()->DrawIndexedInstanced( indexCount, instance_count, 0, 0, 0 );
			break;
		case by_auto:
			m_dmd3d->GetDeviceContext()->DrawAuto();
			break;
		default:
			break;
	}

	return;
}

bool DMShader::Prepare( DMCamera* camera, int phase_idx )
{
	if( !selectPhase( phase_idx ) )
		return false;

	DMShader::Phase phase = m_phases[m_phase_idx];

	phase.index_vs;

	setCamera( camera );

	// Set the vertex input layout.
	m_dmd3d->GetDeviceContext()->IASetInputLayout( m_layout[phase.index_vs].get() );

	// Set the vertex and pixel shaders that will be used to render this triangle.
	ID3D11VertexShader* vs = m_vertex_shader[phase.index_vs].get();
	m_dmd3d->GetDeviceContext()->VSSetShader( vs, NULL, 0 );

	if( m_pixel_shader.size() && phase.index_ps >= 0 )
	{
		ID3D11PixelShader* ps = m_pixel_shader[phase.index_ps].get();
		m_dmd3d->GetDeviceContext()->PSSetShader( ps, NULL, 0 );
	}
	else
	{
		m_dmd3d->GetDeviceContext()->PSSetShader( NULL, NULL, 0 );
	}

	if( m_geometry_shader.size() )
	{
		ID3D11GeometryShader* gs = m_geometry_shader[phase.index_gs].get();
		m_dmd3d->GetDeviceContext()->GSSetShader( gs, NULL, 0 );
	}
	else
		m_dmd3d->GetDeviceContext()->GSSetShader( NULL, NULL, 0 );


	prepare();

	return true;
}

bool DMShader::prepare()
{
	return true;
}

bool DMShader::addShaderPass( DMShader::ShaderType type, const char* function_name, const WCHAR* file_name, const std::string& defines )
{
	ID3D10Blob* errorMessage;
	ID3D10Blob* shaderBuffer;
	HRESULT result;
	std::string shader_version( "_4_0" );

	switch( type )
	{
		case DMShader::vs:
			shader_version = "vs" + shader_version;
			break;
		case DMShader::ps:
			shader_version = "ps" + shader_version;
			break;
		case DMShader::gs:
			shader_version = "gs" + shader_version;
			break;
		default:
			break;
	}

	D3D10_SHADER_MACRO* macro = nullptr;

	parse_defines( defines, &macro );

	result = D3DX11CompileFromFile( file_name, macro, NULL, function_name, shader_version.data(), D3D10_SHADER_ENABLE_STRICTNESS, 0, NULL,
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
			MessageBox( 0, file_name, L"Missing Shader File", MB_OK );
		}

		return false;
	}

	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;

	switch( type )
	{
		case DMShader::vs:
			ID3D11VertexShader* vertex_shader;
			result = m_dmd3d->GetDevice()->CreateVertexShader( shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), NULL, &vertex_shader );
			if( FAILED( result ) )
			{
				return false;
			}
			m_vertex_shader.push_back( make_com_ptr<ID3D11VertexShader>( vertex_shader ) );

			vertex_layout = initLayouts();

			ID3D11InputLayout* layout;
			// Create the vertex input layout.
			if( vertex_layout.size() )
				result = m_dmd3d->GetDevice()->CreateInputLayout( &vertex_layout[0], vertex_layout.size(), shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), &layout );
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
		case DMShader::ps:
			// Create the pixel shader from the buffer.
			ID3D11PixelShader* pixel_shader;
			result = m_dmd3d->GetDevice()->CreatePixelShader( shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), NULL, &pixel_shader );
			if( FAILED( result ) )
			{
				return false;
			}
			m_pixel_shader.push_back( make_com_ptr<ID3D11PixelShader>( pixel_shader ) );
			break;
		case DMShader::gs:
			ID3D11GeometryShader* geometry_shader;
			if( m_use_strimout_gs )
			{
				D3D11_SO_DECLARATION_ENTRY decl;
				StrimOutputDeclaration( &decl );
				UINT strides = sizeof( D3DXVECTOR4 );
				result = m_dmd3d->GetDevice()->CreateGeometryShaderWithStreamOutput( shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), &decl, 1, 
																							NULL, 0, 0, 0, &geometry_shader );
			}
			else
			{
				result = m_dmd3d->GetDevice()->CreateGeometryShader( shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), NULL, &geometry_shader );
			}
			if( FAILED( result ) )
			{
				return false;
			}
			m_geometry_shader.push_back( make_com_ptr<ID3D11GeometryShader>( geometry_shader ) );
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
	m_use_strimout_gs = use_strimout_gs;
}

void DMShader::createPhase( int index_vs, int index_gs, int index_ps )
{
	int v_size = m_vertex_shader.size();
	int g_size = m_geometry_shader.size();
	int p_size = m_pixel_shader.size();

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

	for( auto phase: m_phases )
	{
		if( phase == new_phase  )
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
		m_phase_idx = idx;
		return true;
	}

	return false;
}

void DMShader::setDrawType( DrawType type )
{
	m_draw_type = type;
}

int DMShader::phase()
{
	return m_phase_idx;
}

void DMShader::parse_defines( std::string defines, D3D10_SHADER_MACRO** macro_result )
{	

	if( !defines.size() )
		return;

	std::vector<std::string> comma_split;

	str_split( defines, comma_split, "," );

	D3D10_SHADER_MACRO* macro = new D3D10_SHADER_MACRO[ comma_split.size() + 1 ];

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
