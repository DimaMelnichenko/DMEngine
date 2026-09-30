#pragma once

#include <cstdint>
#include <vector>
#include "GpuResources.h"

// Куча дескрипторов со списком свободных: дескриптор выдаётся навсегда (bindless — его номер знает шейдер) и
// возвращается при удалении вида. Shader-visible — одна куча CBV/SRV/UAV на всё; RTV, DSV и копии UAV для очисток —
// кучи без доступа шейдеров
class DescriptorHeap
{
public:
	bool create( ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible, const wchar_t* name )
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = type;
		desc.NumDescriptors = capacity;
		desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ID3D12DescriptorHeap* raw = nullptr;
		if( FAILED( device->CreateDescriptorHeap( &desc, __uuidof( ID3D12DescriptorHeap ), reinterpret_cast<void**>( &raw ) ) ) )
			return false;
		raw->SetName( name );
		m_heap.reset( raw );
		m_capacity = capacity;
		m_increment = device->GetDescriptorHandleIncrementSize( type );
		m_cpuStart = raw->GetCPUDescriptorHandleForHeapStart();
		m_gpuStart = shaderVisible ? raw->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
		m_shaderVisible = shaderVisible;
		m_next = 0;
		m_freeList.clear();
		return true;
	}

	bool valid() const { return m_heap != nullptr; }
	ID3D12DescriptorHeap* handle() const { return m_heap.get(); }
	uint32_t capacity() const { return m_capacity; }
	uint32_t used() const { return m_next - static_cast<uint32_t>( m_freeList.size() ); }

	// Свободный дескриптор; невалидный — куча полна (в лог пишет DMD3D)
	Descriptor allocate()
	{
		uint32_t index;
		if( !m_freeList.empty() )
		{
			index = m_freeList.back();
			m_freeList.pop_back();
		}
		else if( m_next < m_capacity )
			index = m_next++;
		else
			return {};
		return at( index );
	}

	void free( const Descriptor& descriptor )
	{
		if( descriptor.valid() && descriptor.index < m_capacity )
			m_freeList.push_back( descriptor.index );
	}

	Descriptor at( uint32_t index ) const
	{
		Descriptor descriptor;
		descriptor.index = index;
		descriptor.cpu.ptr = m_cpuStart.ptr + static_cast<SIZE_T>( index ) * m_increment;
		if( m_shaderVisible )
			descriptor.gpu.ptr = m_gpuStart.ptr + static_cast<UINT64>( index ) * m_increment;
		return descriptor;
	}

	// Номер дескриптора по его CPU-адресу (callback'и ImGui отдают только адреса)
	uint32_t indexOf( D3D12_CPU_DESCRIPTOR_HANDLE cpu ) const
	{
		return m_increment ? static_cast<uint32_t>( ( cpu.ptr - m_cpuStart.ptr ) / m_increment ) : Descriptor::invalidIndex;
	}

private:
	com_unique_ptr<ID3D12DescriptorHeap> m_heap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
	D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};
	uint32_t m_increment = 0;
	uint32_t m_capacity = 0;
	uint32_t m_next = 0;
	bool m_shaderVisible = false;
	std::vector<uint32_t> m_freeList;
};
