#pragma once

#include "DirectX.h"
#include <memory>

#include "D3D\DMD3D.h"

// Compute-шейдер: байткод DXC и свой пайплайн, собранный при загрузке. Параметры прохода — константные буферы объекта
// (b4 и дальше: у каждого прохода свой файл констант, Shaders/<подсистема>_constants.h), ресурсы — setUAVBuffer и
// DMD3D::setSRV, сетка групп — явно (dispatchGroups): размер, который проход обходит, шейдер берёт из своих констант
class ComputeShader
{
public:
	ComputeShader();
	~ComputeShader();

	// reproducible — одинаковый байткод в Debug и Release (ShaderCompiler::compile): шейдеры, чей результат кэшируется на диске;
	// defines — "A=1,B", как у ShaderProgram::addShaderPassFromFile
	bool initialize( const std::string& file_name, const std::string& function_name, bool reproducible = false,
					 const std::string& defines = "" );
	void setUAVBuffer( int index, const StorageView& view );
	// Сетка групп x × y × z (группа — numthreads шейдера)
	void dispatchGroups( uint32_t x, uint32_t y, uint32_t z );

private:
	ShaderStage m_computeShader;
};
