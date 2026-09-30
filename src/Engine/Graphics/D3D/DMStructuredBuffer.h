#pragma once

#include "D3D/DMD3D.h"
#include "DM3DUtils.h"

// Структурный буфер, который пишут с CPU каждый кадр (инстансы моделей, патчи террейна) и читает вершинный шейдер
class DMStructuredBuffer
{
public:
	DMStructuredBuffer();
	~DMStructuredBuffer();

	void createBuffer( size_t sizeOfElement, size_t countElements );
	void updateData( const void* data, size_t sizeInByte );
	void setToSlot( int8_t slot, SRVType type );
	uint32_t sizeofElement() const;
	uint32_t numElements() const;

	template<class ResourceType>
	void updateData( Device::CopyFunc<ResourceType> function )
	{
		Device::updateResource<ResourceType>( m_buffer, function );
	}

private:
	Buffer m_buffer;
	ShaderView m_view;
	size_t m_sizeOfElement = 0;
	size_t m_countElements = 0;
};
