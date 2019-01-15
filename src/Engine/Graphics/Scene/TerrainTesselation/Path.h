#pragma once

#include "Utils/utilites.h"

class Path
{
public:
	Path();
	~Path();

	bool reinitializeBuffers( uint16_t pathSize = 8 );

	uint32_t indexCount();
	ID3D11Buffer* vertexBuffer();
	ID3D11Buffer* indexBuffer();

private:
	com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
	com_unique_ptr<ID3D11Buffer> m_indexBuffer;
	uint32_t m_indexCount;
};

