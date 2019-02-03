#pragma once

#include "D3D/DMD3D.h"
#include "DM3DUtils.h"

class DMStructuredBuffer
{
public:
	DMStructuredBuffer();
	~DMStructuredBuffer();
	
	void createBuffer( size_t sizeOfElement, size_t countElements );
	void updateData( void* data, size_t sizeInByte );
	void setToSlot( int8_t slot, SRVType type );
	uint32_t sizeofElement() const;
	uint32_t numElements() const;

	template<class ResourceType>
	void updateData( Device::CopyFunc<ResourceType> function )
	{
		Device::updateResource<ResourceType>( m_buffer, function );
	}

private:
	com_unique_ptr<ID3D11Buffer> m_buffer;
	com_unique_ptr<ID3D11ShaderResourceView> m_SRV;
	size_t m_sizeOfElement;
	size_t m_countElements;
};

