#include "ComputeShader.h"
#include "D3D\ShaderCompiler.h"
#include "Logger\Logger.h"


ComputeShader::ComputeShader()
{
}


ComputeShader::~ComputeShader()
{
}

bool ComputeShader::initialize( const std::string& file_name, const std::string& function_name, bool reproducible,
								const std::string& defines )
{
	// DXC → DXIL cs_6_6 с кэшем на диске; пайплайн собирается сразу — в кадре сборка PSO была бы фризом
	std::vector<uint8_t> bytecode;
	const std::string profile = ShaderCompiler::profile( ShaderStageType::compute );
	if( !ShaderCompiler::instance().compile( file_name, function_name, profile, defines, bytecode, reproducible ) )
		return false;
	if( !DMD3D::instance().createShaderStage( ShaderStageType::compute, bytecode.data(), bytecode.size(), m_computeShader ) )
		return false;
	DMD3D::instance().warmComputePipeline( m_computeShader );
	return true;
}

void ComputeShader::setUAVBuffer( int index, const StorageView& view )
{
	if( index < 0 || index > 7 )
		return;

	DMD3D::instance().setUAV( static_cast<uint16_t>( index ), view );
}

void ComputeShader::dispatchGroups( uint32_t x, uint32_t y, uint32_t z )
{
	DMD3D::instance().setComputeShader( m_computeShader );
	DMD3D::instance().dispatch( x, y, z );
}
