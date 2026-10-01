#pragma once

#include "D3D/DMD3D.h"

// Структурный буфер, который пишут с CPU каждый кадр (инстансы моделей, патчи террейна, свет) и читает шейдер: данные
// живут в участке кольца кадра DMD3D (updateData), привязка — временный SRV участка (setToSlot). Записанное читается
// только в этом кадре: писать надо каждый кадр перед привязкой
class DMStructuredBuffer
{
public:
	DMStructuredBuffer();
	~DMStructuredBuffer();

	void createBuffer( size_t sizeOfElement, size_t countElements, const char* name = nullptr );
	void updateData( const void* data, size_t sizeInByte );
	void setToSlot( int8_t slot );
	uint32_t sizeofElement() const;
	uint32_t numElements() const;

	template<class ResourceType>
	void updateData( Device::CopyFunc<ResourceType> function )
	{
		Device::updateResource<ResourceType>( m_buffer, function );
	}

private:
	Buffer m_buffer;
	size_t m_sizeOfElement = 0;
	size_t m_countElements = 0;
};
