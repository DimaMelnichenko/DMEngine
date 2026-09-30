#include "DMShader.h"
#include <algorithm>
#include <assert.h>
#include "Logger\Logger.h"
#include "D3D\ShaderCompiler.h"

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
		default:
			break;
	}
}

bool DMShader::setPass( int phase_idx )
{
	if( !selectPhase( phase_idx ) )
		return false;

	// Пайплайн фазы под текущее состояние и цели текущего прохода
	DMD3D& d3d = DMD3D::instance();
	d3d.setPipeline( d3d.pipeline( pipelineDesc( m_phaseIdx, d3d.renderState(), d3d.passFormats() ) ) );

	prepare();

	return true;
}

PipelineDesc DMShader::pipelineDesc( int phaseIndex, const RenderState& state, const TargetFormats& formats ) const
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
	desc.formats = formats;
	return desc;
}

void DMShader::warmPipelines( const std::vector<RenderState>& states, const TargetFormats& formats )
{
	DMD3D& d3d = DMD3D::instance();
	for( int phase = 0; phase < phaseCount(); ++phase )
		for( const RenderState& state : states )
			d3d.pipeline( pipelineDesc( phase, state, formats ) );
}

void DMShader::warmPipelines( const std::vector<RenderState>& states, const TargetFormats& formats, const std::vector<int>& phases )
{
	DMD3D& d3d = DMD3D::instance();
	for( int phase : phases )
		if( phase >= 0 && phase < phaseCount() )
			for( const RenderState& state : states )
				d3d.pipeline( pipelineDesc( phase, state, formats ) );
}

std::vector<int> DMShader::depthPhases() const
{
	std::vector<int> phases;
	for( int phase = 0; phase < phaseCount(); ++phase )
		if( m_phases[phase].index_ps < 0 )
			phases.push_back( phase );
	return phases;
}

std::vector<int> DMShader::colorPhases() const
{
	const std::vector<int> depth = depthPhases();
	std::vector<int> phases;
	for( int phase = 0; phase < phaseCount(); ++phase )
		if( std::find( depth.begin(), depth.end(), phase ) == depth.end() )
			phases.push_back( phase );
	return phases;
}

bool DMShader::prepare()
{
	return true;
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
	// DXC → DXIL SM 6.6 с кэшем на диске (D3D/ShaderCompiler.h); ошибки — в лог и shader-error.txt
	std::vector<uint8_t> bytecode;
	if( !ShaderCompiler::instance().compile( file_name, function_name, ShaderCompiler::profile( type ), defines, bytecode ) )
		return false;

	if( !createShaderPass( type, bytecode ) )
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

bool DMShader::createShaderPass( SRVType type, const std::vector<uint8_t>& bytecode )
{
	DMD3D& d3d = DMD3D::instance();
	ShaderStage stage;
	if( !d3d.createShaderStage( type, bytecode.data(), bytecode.size(), stage ) )
		return false;

	switch( type )
	{
		case SRVType::vs:
			m_vertexShader.push_back( std::move( stage ) );
			// Раскладка вершин — с первым вершинным шейдером (байткод ей не нужен)
			if( !m_layoutDesc.empty() && !d3d.createInputLayout( m_layoutDesc, bytecode.data(), bytecode.size(), m_layout ) )
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
