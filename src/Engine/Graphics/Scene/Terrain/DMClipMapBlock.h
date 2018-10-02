#pragma once

#include <stdint.h>
#include "DirectX.h"
#include "D3D/DMD3D.h"

class DMClipMapBlock
{
public:
	DMClipMapBlock();
	~DMClipMapBlock();

	void Initialize( uint16_t width, uint16_t height );
	ID3D11Buffer* structuredBuffer();
	ID3D11Buffer* indexBuffer();
	uint32_t vertexCount();
	uint32_t indexCount();

private:
	com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
	com_unique_ptr<ID3D11Buffer> m_indexBuffer;
	uint32_t m_vertexCount;
	uint32_t m_indexCount;
};

