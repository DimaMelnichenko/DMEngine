#include "DMComputeShader.h"
#include "Shaders\slots.h"
#include "D3D\ShaderCompiler.h"
#include "Logger\Logger.h"


DMComputeShader::DMComputeShader()
{
}


DMComputeShader::~DMComputeShader()
{
}

bool DMComputeShader::Initialize( const std::string& file_name, const std::string& function_name )
{
	// DXC → DXIL cs_6_6 с кэшем на диске; пайплайн собирается сразу — в кадре сборка PSO была бы фризом
	std::vector<uint8_t> bytecode;
	const std::string profile = ShaderCompiler::profile( ShaderStageType::compute );
	if( !ShaderCompiler::instance().compile( file_name, function_name, profile, "", bytecode ) )
		return false;
	if( !DMD3D::instance().createShaderStage( ShaderStageType::compute, bytecode.data(), bytecode.size(), m_computeShader ) )
		return false;
	DMD3D::instance().warmComputePipeline( m_computeShader );

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( ConstantType ), m_constantBuffer ) )
		return false;

	return true;
}

void DMComputeShader::setUAVBuffer( int index, const StorageView& view )
{
	if( index < 0 || index > 7 )
		return;

	DMD3D::instance().setUAV( static_cast<uint16_t>( index ), view );
}

void DMComputeShader::Dispatch( uint32_t numElements, float elapsed_time )
{
	DMD3D::instance().setComputeShader( m_computeShader );

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

	ConstantType constantType = {};
	constantType.groupDim = group_size_X;
	constantType.rect.x = numElements;
	constantType.elapsedTime = elapsed_time;

	setConstants( constantType );

	//////////////////////////////////////
	//////////	DISPATCH	///////////////

	DMD3D::instance().dispatch( group_size_X, group_size_Y, 1 );

}


void DMComputeShader::Dispatch( uint16_t width, uint16_t height, float elapsed_time )
{
	DMD3D::instance().setComputeShader( m_computeShader );

	//////////////////////////////////////
	//	calc
	int group_size_X = ( width % 32 != 0 ) ? ( ( width / 32 ) + 1 ) : ( width / 32 );
	int group_size_Y = ( height % 32 != 0 ) ? ( ( height / 32 ) + 1 ) : ( height / 32 );

	///////////////////////////////////
	// set contant

	ConstantType constantType = {};
	constantType.groupDim = group_size_X;
	constantType.rect.x = width;
	constantType.rect.y = height;
	constantType.elapsedTime = elapsed_time;

	setConstants( constantType );

	//////////////////////////////////////
	//////////	DISPATCH /////////////////

	DMD3D::instance().dispatch( group_size_X, group_size_Y, 1 );

}

void DMComputeShader::dispatchGroups( uint32_t x, uint32_t y, uint32_t z )
{
	DMD3D::instance().setComputeShader( m_computeShader );
	DMD3D::instance().dispatch( x, y, z );
}

void DMComputeShader::setConstants( ConstantType& constantType )
{
	Device::updateResourceData( m_constantBuffer, constantType );

	DMD3D::instance().setConstantBuffer( SLOT_CB_PASS, m_constantBuffer );
}
