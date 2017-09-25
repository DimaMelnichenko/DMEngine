#pragma once

#include "DM3DObject.h"
#include "DM3DUtils.h"

class DMStructuredBuffer : public DM3DObject
{
public:
	DMStructuredBuffer( DMD3D* dmd3d );
	~DMStructuredBuffer();
	
	void CreateBuffer( size_t sizeOfElement, size_t countElements );
	void UpdateData( void* data, size_t sizeInByte );
	void setToSlot( int8_t slot, SRVType type );

private:
	com_unique_ptr<ID3D11Buffer> m_buffer;
	com_unique_ptr<ID3D11ShaderResourceView> m_SRV;
	size_t m_sizeOfElement;
	size_t m_countElements;
};

