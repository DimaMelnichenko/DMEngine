#pragma once

#include "DirectX.h"
#include <memory>

#include "D3D\DMD3D.h"

class DMComputeShader
{
public:
	DMComputeShader();
	~DMComputeShader();

	bool Initialize( const std::string& file_name, const std::string& function_name );
	void setUAVBuffer( int index, const StorageView& view );
	void Dispatch( uint16_t width, uint16_t height, float elapsed_time );
	void Dispatch( uint32_t numElements, float elapsed_time );
	// Сетка групп — явно; свои константы (ThreadsData, b2) не пишет: параметры прохода — в b4 и дальше
	void dispatchGroups( uint32_t x, uint32_t y, uint32_t z );

private:
	struct alignas( 16 ) ConstantType
	{
		float groupDim;
		DirectX::XMFLOAT2 rect;
		float elapsedTime;
	};

	void setConstants( ConstantType& constantType );
private:
	ShaderStage m_computeShader;
	Buffer m_constantBuffer;
};
