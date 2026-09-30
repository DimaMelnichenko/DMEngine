#include "DMShader.h"
#include <assert.h>
#include "Logger\Logger.h"
#include <d3dcompiler.h>
#include "ShaderUtils.h"

namespace GS
{

DMShader::DMShader() :
	m_phaseIdx( 0 )
{

}

DMShader::~DMShader()
{

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
	DMD3D& d3d = DMD3D::instance();
	switch( m_drawType )
	{
		case by_vertex:
			d3d.draw( indexCount, vertexOffset );
			break;
		case by_index:
			d3d.drawIndexed( indexCount, indexOffset, vertexOffset );
			break;
		case by_index_instance:
			d3d.drawIndexedInstanced( indexCount, instance_count, indexOffset, vertexOffset, 0 );
			break;
		case by_auto:
			d3d.drawAuto();
			break;
		default:
			break;
	}
}

void DMShader::renderInstancedIndirect( const Buffer& args, uint32_t argsOffset )
{
	DMD3D::instance().drawIndexedInstancedIndirect( args, argsOffset );
}

bool DMShader::setPass( int phase_idx )
{
	if( !selectPhase( phase_idx ) )
		return false;

	DMD3D& d3d = DMD3D::instance();
	d3d.setPipeline( d3d.pipeline( pipelineDesc( m_phaseIdx, d3d.renderState() ) ) );

	prepare();

	return true;
}

PipelineDesc DMShader::pipelineDesc( int phaseIndex, const RenderState& state ) const
{
	const Phase& phase = m_phases[phaseIndex];
	PipelineDesc desc;
	desc.vertex = stage( m_vertexShader, phase.index_vs );
	desc.pixel = stage( m_pixelShader, phase.index_ps );
	desc.geometry = stage( m_geometryShader, phase.index_gs );
	desc.hull = stage( m_hullShader, phase.index_hs );
	desc.domain = stage( m_domainShader, phase.index_ds );
	desc.layout = m_layout.valid() ? &m_layout : nullptr;
	desc.state = state;
	desc.topology = m_topology;
	return desc;
}

void DMShader::warmPipelines( const std::vector<RenderState>& states )
{
	DMD3D& d3d = DMD3D::instance();
	for( int phase = 0; phase < phaseCount(); ++phase )
		for( const RenderState& state : states )
			d3d.pipeline( pipelineDesc( phase, state ) );
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

void DMShader::setLayoutDesc( std::vector<VertexElement>&& layoutDesc )
{
	m_layoutDesc = std::move( layoutDesc );
}

bool DMShader::addShaderPassFromFile( SRVType type,
									  const std::string& function_name,
									  const std::string& file_name,
									  const std::string& defines )
{
	std::vector<D3D_SHADER_MACRO> macros;

	parseDefines( defines, macros );

	std::wstring fileName = utf8ToWide( file_name );

	ID3DBlob* buffer = nullptr;
	ID3DBlob* error = nullptr;
	HRESULT result = D3DCompileFromFile( fileName.data(),
										 macros.empty() ? nullptr : &macros[0],
										 D3D_COMPILE_STANDARD_FILE_INCLUDE, function_name.data(),
										 version( type ).data(),
										 shaderCompileFlags(),
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

	if( !createShaderPass( type, shaderBuffer ) )
		return false;

	m_sources.push_back( { type, function_name, file_name, defines } );
	return true;
}

std::optional<DMShader::ShaderSource> DMShader::shaderSource( SRVType type ) const
{
	for( const ShaderSource& source : m_sources )
	{
		if( source.type == type )
			return source;
	}
	return std::nullopt;
}

bool DMShader::createShaderPass( SRVType type, com_unique_ptr<ID3DBlob>& shaderBuffer )
{
	DMD3D& d3d = DMD3D::instance();
	ShaderStage stage;
	if( !d3d.createShaderStage( type, shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), stage ) )
		return false;

	switch( type )
	{
		case SRVType::vs:
			m_vertexShader.push_back( std::move( stage ) );
			// Раскладка вершин — под байткод первого вершинного шейдера
			if( !m_layoutDesc.empty() &&
				!d3d.createInputLayout( m_layoutDesc, shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), m_layout ) )
				return false;
			break;
		case SRVType::ps:
			m_pixelShader.push_back( std::move( stage ) );
			break;
		case SRVType::gs:
			m_geometryShader.push_back( std::move( stage ) );
			break;
		case SRVType::hs:
			m_hullShader.push_back( std::move( stage ) );
			break;
		case SRVType::ds:
			m_domainShader.push_back( std::move( stage ) );
			break;
		default:
			return false;
	}

	return true;
}

int DMShader::createPhase( int index_vs, int index_ps, int index_gs, int index_hs, int index_ds )
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
		return -1;
	}

	Phase new_phase;
	new_phase.index_vs = index_vs;
	new_phase.index_gs = index_gs;
	new_phase.index_ps = index_ps;
	new_phase.index_hs = index_hs;
	new_phase.index_ds = index_ds;

	for( size_t i = 0; i < m_phases.size(); ++i )
	{
		if( m_phases[i] == new_phase )
			return static_cast<int>( i );
	}

	m_phases.push_back( new_phase );
	return static_cast<int>( m_phases.size() ) - 1;
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

std::vector<VertexElement> DMShader::initLayouts()
{
	return std::vector<VertexElement>();
}

}
