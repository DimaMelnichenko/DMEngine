#pragma once

#include "DirectX.h"
#include <memory>

#include "D3D\DMD3D.h"
#include "Shaders\compute_dispatch_constants.h"

class DMComputeShader
{
public:
	DMComputeShader();
	~DMComputeShader();

	// reproducible — одинаковый байткод в Debug и Release (ShaderCompiler::compile): шейдеры, чей результат кэшируется на диске;
	// defines — "A=1,B", как у ShaderProgram::addShaderPassFromFile
	bool Initialize( const std::string& file_name, const std::string& function_name, bool reproducible = false,
					 const std::string& defines = "" );
	void setUAVBuffer( int index, const StorageView& view );
	void Dispatch( uint16_t width, uint16_t height, float elapsed_time );
	void Dispatch( uint32_t numElements, float elapsed_time );
	// Сетка групп — явно; свои константы (ThreadsData, b2) не пишет: параметры прохода — в b4 и дальше
	void dispatchGroups( uint32_t x, uint32_t y, uint32_t z );

private:

	void setConstants( GS::ThreadsData& constantType );
private:
	ShaderStage m_computeShader;
	Buffer m_constantBuffer;
};
