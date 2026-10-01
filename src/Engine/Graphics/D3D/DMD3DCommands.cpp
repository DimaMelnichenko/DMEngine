// DMD3D: привязка (таблица привязок вызова, root CBV, буферы вершин и индексов), вызовы и dispatch, запросы времени
// GPU и метки PIX
#include "DMD3D.h"
#include "Shaders\slots.h"
#include "Logger\Logger.h"
// Метки PIX и в Release: без захвата это только запись маркеров в командный список
#ifndef USE_PIX
#define USE_PIX
#endif
#include <pix3.h>
#include <algorithm>
#include <string>

namespace
{

// Слот таблицы привязок для слота SRV (Shaders/bindless.sh); −1 — слот вне таблицы
int bindingIndex( uint16_t slot )
{
	if( slot < SLOT_TRANSIENT_COUNT )
		return slot;
	if( slot >= SLOT_SCENE_FIRST && slot < SLOT_SCENE_FIRST + SLOT_SCENE_COUNT )
		return DM_BINDING_SCENE_BASE + slot - SLOT_SCENE_FIRST;
	return -1;
}

}

void DMD3D::setBinding( uint32_t index, uint32_t descriptorIndex )
{
	if( index >= DM_BINDING_COUNT || m_bindings[index] == descriptorIndex )
		return;
	m_bindings[index] = descriptorIndex;
	m_bindingsDirtyGraphics = m_bindingsDirtyCompute = true;
}

bool DMD3D::setConstantBuffer( uint16_t slot, const Buffer& buffer )
{
	if( slot >= SLOT_CB_COUNT || ( buffer.ring() && !buffer.ringConstant() ) )
		return false;
	// Участок этого кадра; без записи в кадре привязывать нечего
	if( buffer.ring() && buffer.ringBytes() == 0 )
		return false;
	const D3D12_GPU_VIRTUAL_ADDRESS address = buffer.ring() ? m_constantRing.address( buffer.ringOffset() ) : buffer.gpuAddress();
	if( !address )
		return false;
	if( m_rootCBV[slot] != address )
	{
		m_rootCBV[slot] = address;
		m_cbvDirtyGraphics |= 1u << slot;
		m_cbvDirtyCompute |= 1u << slot;
	}
	return true;
}

void DMD3D::setSRV( uint16_t slot, const ShaderView& view )
{
	const int index = bindingIndex( slot );
	if( index < 0 )
		return;
	// Вид ресурса, который пишет текущий проход, на входе: проход над ним закончен — барьер в состояние чтения
	if( view.valid() )
		barrier( view.resource(), D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, &view.range() );
	setBinding( static_cast<uint32_t>( index ), view.valid() ? view.index() : 0 );
}

void DMD3D::setSRV( uint16_t slot, const Buffer& ringBuffer )
{
	const int index = bindingIndex( slot );
	if( index < 0 )
		return;
	// Участок кольца в upload-куче: барьеров у него нет, дескриптор — из пула этого кадра (endWrite)
	setBinding( static_cast<uint32_t>( index ), ringBuffer.ring() && ringBuffer.ringBytes() ? ringBuffer.ringView() : 0 );
}

void DMD3D::setUAV( uint16_t slot, const StorageView& view )
{
	if( slot >= SLOT_UAV_COUNT )
		return;
	if( view.valid() )
	{
		barrier( view.resource(), D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, &view.range() );
		m_dispatchWrites.push_back( view.resource() );
	}
	setBinding( DM_BINDING_UAV_BASE + slot, view.valid() ? view.index() : 0 );
}

void DMD3D::clearStorageView( const StorageView& view )
{
	if( !view.valid() )
		return;
	ensureRecording();
	barrier( view.resource(), D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, &view.range() );
	flushBarriers();
	const UINT zeros[4] = {};
	m_commandList->ClearUnorderedAccessViewUint( view.descriptor().gpu, view.clearDescriptor().cpu, view.resource(), zeros, 0, nullptr );
	// Следующая запись через UAV — после барьера
	auto found = m_states.find( view.resource() );
	if( found != m_states.end() )
		for( SubresourceState& sub : found->second.subresources )
			sub.uavWritten = true;
}

void DMD3D::setVertexBuffers( uint32_t count, const Buffer* const buffers[], const uint32_t strides[], const uint32_t offsets[] )
{
	D3D12_VERTEX_BUFFER_VIEW views[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
	count = std::min<uint32_t>( count, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT );
	for( uint32_t i = 0; i < count; ++i )
	{
		const Buffer* buffer = buffers[i];
		if( !buffer || !buffer->handle() )
			continue;
		const uint32_t offset = offsets ? offsets[i] : 0;
		views[i].BufferLocation = buffer->gpuAddress() + offset;
		views[i].SizeInBytes = buffer->size() - std::min( offset, buffer->size() );
		views[i].StrideInBytes = strides[i];
		barrier( buffer->handle(), D3D12_BARRIER_ACCESS_VERTEX_BUFFER );
	}
	m_commandList->IASetVertexBuffers( 0, count, views );
}

void DMD3D::setVertexBuffer( const Buffer& buffer, uint32_t stride, uint32_t offset )
{
	const Buffer* buffers[] = { &buffer };
	setVertexBuffers( 1, buffers, &stride, &offset );
}

void DMD3D::setIndexBuffer( const Buffer& buffer, DXGI_FORMAT format, uint32_t offset )
{
	if( !buffer.handle() )
		return;
	D3D12_INDEX_BUFFER_VIEW view = {};
	view.BufferLocation = buffer.gpuAddress() + offset;
	view.SizeInBytes = buffer.size() - std::min( offset, buffer.size() );
	view.Format = format;
	barrier( buffer.handle(), D3D12_BARRIER_ACCESS_INDEX_BUFFER );
	m_commandList->IASetIndexBuffer( &view );
}

void DMD3D::unbindGeometry()
{
	// Вершины по SV_VertexID: буферов вершин и индексов нет
	const D3D12_VERTEX_BUFFER_VIEW empty[2] = {};
	m_commandList->IASetVertexBuffers( 0, 2, empty );
	m_commandList->IASetIndexBuffer( nullptr );
}

void DMD3D::flushGraphicsRoot()
{
	if( m_bindingsDirtyGraphics )
	{
		m_commandList->SetGraphicsRoot32BitConstants( 0, DM_BINDING_COUNT, m_bindings, 0 );
		m_bindingsDirtyGraphics = false;
	}
	for( uint32_t slot = 0; slot < SLOT_CB_COUNT && m_cbvDirtyGraphics; ++slot )
	{
		if( ( m_cbvDirtyGraphics & ( 1u << slot ) ) && m_rootCBV[slot] )
			m_commandList->SetGraphicsRootConstantBufferView( 1 + slot, m_rootCBV[slot] );
		m_cbvDirtyGraphics &= ~( 1u << slot );
	}
}

void DMD3D::flushComputeRoot()
{
	if( m_bindingsDirtyCompute )
	{
		m_commandList->SetComputeRoot32BitConstants( 0, DM_BINDING_COUNT, m_bindings, 0 );
		m_bindingsDirtyCompute = false;
	}
	for( uint32_t slot = 0; slot < SLOT_CB_COUNT && m_cbvDirtyCompute; ++slot )
	{
		if( ( m_cbvDirtyCompute & ( 1u << slot ) ) && m_rootCBV[slot] )
			m_commandList->SetComputeRootConstantBufferView( 1 + slot, m_rootCBV[slot] );
		m_cbvDirtyCompute &= ~( 1u << slot );
	}
}

void DMD3D::draw( uint32_t vertexCount, uint32_t startVertex )
{
	if( !m_graphicsPipelineValid )
		return;
	flushBarriers();
	flushGraphicsRoot();
	m_commandList->DrawInstanced( vertexCount, 1, startVertex, 0 );
}

void DMD3D::drawIndexed( uint32_t indexCount, uint32_t startIndex, int32_t baseVertex )
{
	if( !m_graphicsPipelineValid )
		return;
	flushBarriers();
	flushGraphicsRoot();
	m_commandList->DrawIndexedInstanced( indexCount, 1, startIndex, baseVertex, 0 );
}

void DMD3D::drawIndexedInstanced( uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance )
{
	if( !m_graphicsPipelineValid )
		return;
	flushBarriers();
	flushGraphicsRoot();
	m_commandList->DrawIndexedInstanced( indexCount, instanceCount, startIndex, baseVertex, startInstance );
}

void DMD3D::drawIndexedInstancedIndirectCount( const Buffer& commands, uint32_t commandsOffset, uint32_t maxCommands, const Buffer& counts,
											   uint32_t countOffset )
{
	if( !m_graphicsPipelineValid || !commands.handle() || !counts.handle() || maxCommands == 0 )
		return;
	barrier( commands.handle(), D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT );
	barrier( counts.handle(), D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT );
	flushBarriers();
	flushGraphicsRoot();
	m_commandList->ExecuteIndirect( m_drawIndexedCountSignature.get(), maxCommands, commands.handle(), commandsOffset, counts.handle(), countOffset );
	++m_frameIndirectDraws;
}

void DMD3D::dispatch( uint32_t x, uint32_t y, uint32_t z )
{
	if( m_computePipelineValid )
	{
		flushBarriers();
		flushComputeRoot();
		m_commandList->Dispatch( x, y, z );
	}
	// Записанные UAV: следующая запись в них — после барьера
	for( ID3D12Resource* resource : m_dispatchWrites )
	{
		auto found = m_states.find( resource );
		if( found != m_states.end() )
			for( SubresourceState& sub : found->second.subresources )
				if( sub.access == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS )
					sub.uavWritten = true;
	}
	m_dispatchWrites.clear();
}

bool DMD3D::createTimestampQueries( uint32_t count )
{
	D3D12_QUERY_HEAP_DESC desc = {};
	desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	desc.Count = count;
	ID3D12QueryHeap* heap = nullptr;
	if( FAILED( m_device->CreateQueryHeap( &desc, __uuidof( ID3D12QueryHeap ), reinterpret_cast<void**>( &heap ) ) ) )
	{
		LOG( "CreateQueryHeap (TIMESTAMP, " + std::to_string( count ) + ") failed" );
		return false;
	}
	heap->SetName( L"Timestamp queries" );
	m_timestampHeap = make_com_ptr<ID3D12QueryHeap>( heap );
	m_timestampCount = count;
	if( FAILED( m_directQueue->GetTimestampFrequency( &m_timestampFrequency ) ) )
		m_timestampFrequency = 0;
	return true;
}

void DMD3D::writeTimestamp( uint32_t index )
{
	if( m_timestampHeap && m_recording && index < m_timestampCount )
		m_commandList->EndQuery( m_timestampHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, index );
}

void DMD3D::resolveTimestamps( uint32_t first, uint32_t count, Buffer& destination )
{
	if( !m_timestampHeap || !m_recording || !destination.handle() || first + count > m_timestampCount ||
		static_cast<uint64_t>( count ) * sizeof( uint64_t ) > destination.size() )
		return;
	// Разрешение запросов — не копия: доступ COPY_DEST, стадия — все (у ResolveQueryData в enhanced barriers своя)
	barrier( destination.handle(), D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, true, D3D12_BARRIER_SYNC_ALL );
	flushBarriers();
	m_commandList->ResolveQueryData( m_timestampHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, first, count, destination.handle(), 0 );
}

void DMD3D::beginEvent( const char* name )
{
	if( !m_recording )
		return;
	// Цвет — по глубине вложенности: проходы кадра и объекты внутри них различимы в захвате
	PIXBeginEvent( m_commandList.get(), PIX_COLOR_INDEX( static_cast<BYTE>( m_eventDepth % 7 + 1 ) ), "%s", name ? name : "" );
	++m_eventDepth;
}

void DMD3D::endEvent()
{
	if( !m_recording || m_eventDepth == 0 )
		return;
	--m_eventDepth;
	PIXEndEvent( m_commandList.get() );
}
