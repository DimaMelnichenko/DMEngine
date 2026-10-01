#include "ShaderProgram.h"
#include "D3D\ShaderCompiler.h"

namespace GS
{

bool ShaderProgram::setPass( int phase )
{
	if( phase < 0 || phase >= phaseCount() )
		return false;

	// Пайплайн фазы под текущее состояние и цели текущего прохода
	DMD3D& d3d = DMD3D::instance();
	d3d.setPipeline( d3d.pipeline( pipelineDesc( phase, d3d.renderState(), d3d.passFormats() ) ) );
	return true;
}

PipelineDesc ShaderProgram::pipelineDesc( int phaseIndex, const RenderState& state, const TargetFormats& formats ) const
{
	const Phase& phase = m_phases[phaseIndex];
	PipelineDesc desc;
	desc.vertex = stage( m_vertexShader, phase.index_vs );
	desc.pixel = stage( m_pixelShader, phase.index_ps );
	desc.geometry = stage( m_geometryShader, phase.index_gs );
	desc.layout = m_layout.valid() ? &m_layout : nullptr;
	desc.state = state;
	desc.topology = m_topology;
	desc.formats = formats;
	return desc;
}

void ShaderProgram::warmPipelines( const std::vector<RenderState>& states, const TargetFormats& formats )
{
	DMD3D& d3d = DMD3D::instance();
	for( int phase = 0; phase < phaseCount(); ++phase )
		for( const RenderState& state : states )
			d3d.warmPipeline( pipelineDesc( phase, state, formats ) );
}

void ShaderProgram::warmPipelines( const std::vector<RenderState>& states, const TargetFormats& formats, const std::vector<int>& phases )
{
	DMD3D& d3d = DMD3D::instance();
	for( int phase : phases )
		if( phase >= 0 && phase < phaseCount() )
			for( const RenderState& state : states )
				d3d.warmPipeline( pipelineDesc( phase, state, formats ) );
}

void ShaderProgram::setLayoutDesc( std::vector<VertexElement>&& layoutDesc )
{
	m_layoutDesc = std::move( layoutDesc );
}

bool ShaderProgram::addShaderPassFromFile( ShaderStageType type,
										   const std::string& function_name,
										   const std::string& file_name,
										   const std::string& defines )
{
	std::vector<uint8_t> bytecode;
	if( !ShaderCompiler::instance().compile( file_name, function_name, ShaderCompiler::profile( type ), defines, bytecode ) )
		return false;

	if( !createShaderPass( type, bytecode ) )
		return false;

	m_sources.push_back( { type, function_name, file_name, defines } );
	return true;
}

std::optional<ShaderProgram::ShaderSource> ShaderProgram::shaderSource( ShaderStageType type ) const
{
	for( const ShaderSource& source : m_sources )
	{
		if( source.type == type )
			return source;
	}
	return std::nullopt;
}

bool ShaderProgram::createShaderPass( ShaderStageType type, const std::vector<uint8_t>& bytecode )
{
	DMD3D& d3d = DMD3D::instance();
	ShaderStage stage;
	if( !d3d.createShaderStage( type, bytecode.data(), bytecode.size(), stage ) )
		return false;

	switch( type )
	{
		case ShaderStageType::vertex:
			m_vertexShader.push_back( std::move( stage ) );
			// Раскладка вершин — с первым вершинным шейдером (байткод ей не нужен)
			if( !m_layoutDesc.empty() && !d3d.createInputLayout( m_layoutDesc, bytecode.data(), bytecode.size(), m_layout ) )
				return false;
			break;
		case ShaderStageType::pixel:
			m_pixelShader.push_back( std::move( stage ) );
			break;
		case ShaderStageType::geometry:
			m_geometryShader.push_back( std::move( stage ) );
			break;
		default:
			return false;
	}

	return true;
}

int ShaderProgram::createPhase( int index_vs, int index_ps, int index_gs )
{
	if( index_vs >= static_cast<int>( m_vertexShader.size() ) ||
		index_ps >= static_cast<int>( m_pixelShader.size() ) ||
		index_gs >= static_cast<int>( m_geometryShader.size() ) )
	{
		return -1;
	}

	const Phase phase{ index_vs, index_ps, index_gs };
	for( size_t i = 0; i < m_phases.size(); ++i )
	{
		if( m_phases[i] == phase )
			return static_cast<int>( i );
	}

	m_phases.push_back( phase );
	return static_cast<int>( m_phases.size() ) - 1;
}

}
