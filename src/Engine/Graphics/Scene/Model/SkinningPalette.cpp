#include "SkinningPalette.h"
#include <algorithm>
#include "Shaders\slots.h"
#include "Engine\Animation\Skeleton.h"

using namespace DirectX;

namespace GS
{

void appendBoneLines( const Skeleton& skeleton, const std::vector<XMFLOAT4X4>& model, FXMMATRIX world, std::vector<BoneLine>& lines )
{
	for( size_t i = 0; i < model.size(); ++i )
	{
		const int32_t parent = skeleton.joint( i ).parent;
		if( parent < 0 )
			continue;
		BoneLine line;
		XMStoreFloat3( &line.from, XMVector3Transform( XMVectorSet( model[parent]._41, model[parent]._42, model[parent]._43, 1.0f ), world ) );
		XMStoreFloat3( &line.to, XMVector3Transform( XMVectorSet( model[i]._41, model[i]._42, model[i]._43, 1.0f ), world ) );
		lines.push_back( line );
	}
}

XMFLOAT3X4* SkinningPalette::append( size_t jointCount, uint32_t& offset )
{
	offset = static_cast<uint32_t>( m_matrices.size() );
	m_matrices.resize( m_matrices.size() + jointCount );
	return m_matrices.data() + offset;
}

void SkinningPalette::upload()
{
	if( m_matrices.empty() )
		return;
	if( m_matrices.size() > m_capacity )
	{
		// Буфер — участок кольца кадра: размер — лишь предел записи, растёт с запасом. Элемент — float4 (строка матрицы),
		// как StructuredBuffer<float4> в Shaders/skinning.sh
		m_capacity = std::max<size_t>( m_matrices.size() * 2, 1024 );
		m_buffer.createBuffer( sizeof( XMFLOAT4 ), m_capacity * 3, "Skinning matrices" );
	}
	m_buffer.updateData( m_matrices.data(), m_matrices.size() * sizeof( XMFLOAT3X4 ) );
}

void SkinningPalette::bind() const
{
	m_buffer.setToSlot( SLOT_SKIN_MATRICES );
}

}
