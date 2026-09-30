#include "ConstantRing.h"
#include "Logger\Logger.h"
#include <algorithm>

bool ConstantRing::initialize( ID3D12Device* device, uint32_t bytes, uint32_t frames )
{
	m_frames = std::max( frames, 1u );
	m_frameBytes = ( bytes / m_frames ) & ~( constantAlignment - 1 );
	const uint32_t total = m_frameBytes * m_frames;

	// Upload-куча: CPU пишет, GPU читает через PCIe — для констант, которые живут один кадр, это и есть правильное место
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = total;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ID3D12Resource* raw = nullptr;
	if( FAILED( device->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
												 __uuidof( ID3D12Resource ), reinterpret_cast<void**>( &raw ) ) ) )
	{
		LOG( "Constant ring: failed to create the upload buffer" );
		return false;
	}
	raw->SetName( L"Constant ring" );
	m_buffer.reset( raw );

	// Отображение на всё время жизни: у upload-кучи это разрешено и дёшево
	void* mapped = nullptr;
	const D3D12_RANGE noRead = { 0, 0 };
	if( FAILED( raw->Map( 0, &noRead, &mapped ) ) )
	{
		LOG( "Constant ring: failed to map the upload buffer" );
		return false;
	}
	m_mapped = static_cast<uint8_t*>( mapped );
	m_gpuAddress = raw->GetGPUVirtualAddress();
	m_current = {};
	m_last = {};
	m_current.capacity = m_last.capacity = m_frameBytes;
	return true;
}

void ConstantRing::beginFrame( uint32_t frameIndex )
{
	m_last = m_current;
	m_current = {};
	m_current.capacity = m_frameBytes;
	m_frameStart = ( frameIndex % std::max( m_frames, 1u ) ) * m_frameBytes;
	m_head = m_frameStart;
}

void* ConstantRing::beginWrite( uint32_t size, uint32_t& offset, uint32_t& bytes, uint32_t alignment )
{
	if( !m_mapped || m_writing || size == 0 )
		return nullptr;

	alignment = std::max( alignment, 4u );
	const auto align = [alignment]( uint32_t value ) { return ( value + alignment - 1 ) / alignment * alignment; };
	bytes = align( size );
	if( bytes > m_frameBytes )
		return nullptr;
	uint32_t start = align( m_head );
	if( start + bytes > m_frameStart + m_frameBytes )
	{
		// Часть кадра кончилась: начинаем сначала — прошлые участки этого кадра GPU ещё не прочитал, кольцо надо увеличить
		start = m_frameStart;
		++m_current.frameWraps;
	}

	offset = start;
	m_head = start + bytes;
	m_current.frameBytes += bytes;
	++m_current.frameWrites;
	m_writing = true;
	return m_mapped + offset;
}

void ConstantRing::finishWrite()
{
	m_writing = false;
}
