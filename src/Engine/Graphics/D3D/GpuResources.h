#pragma once

// Непрозрачные ресурсы GPU и их виды — ресурсы и дескрипторы D3D12 (docs/d3d12.md §3.3–3.4). Объекты сцены,
// материалы и проходы держат только их и создают через DMD3D по описаниям ниже; ID3D12* живёт внутри Graphics/D3D:
// методы handle() и reset() — для бэкенда (DMD3D, RenderTarget, CubeTarget, DMStructuredBuffer, TextureImages).
// Вид — постоянный дескриптор в куче (bindless): ShaderView и StorageView — в общей shader-visible куче CBV/SRV/UAV,
// TargetView — в куче RTV или DSV. Дескриптор освобождается вместе с видом
#include <cstdint>
#include <utility>
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "DM3DUtils.h"

namespace D3D12MA
{
class Allocation;
}

// ---------------------------------------------------------------------------------------------------------------------
// Описания
// ---------------------------------------------------------------------------------------------------------------------

// Назначение буфера — битовая маска
namespace BufferUsage
{
	enum : uint32_t
	{
		constant = 1 << 0,			// константный буфер шейдера
		vertex = 1 << 1,
		index = 1 << 2,
		shaderResource = 1 << 3,	// читается шейдером (ShaderView)
		unorderedAccess = 1 << 4,	// пишется compute-шейдером (StorageView)
		indirectArgs = 1 << 5,		// аргументы DrawIndexedInstancedIndirect
		structured = 1 << 6,		// структурный: элементы по stride, виды без формата
		raw = 1 << 7,				// байтовый (ByteAddressBuffer): виды с BufferViewDesc::raw
		cpuWrite = 1 << 8,			// пишется с CPU каждый кадр (Device::updateResource): своего ресурса нет, данные — участок
									// кольца кадра в upload-куче (ConstantRing.h); константы привязываются root CBV, структурные
									// и raw — временным SRV на кадр (DMD3D::setSRV( слот, буфер ))
		readback = 1 << 9,			// копия для чтения на CPU (DMD3D::copyBuffer + readBuffer) — readback-куча
	};
}

struct BufferDesc
{
	uint32_t size = 0;			// байты
	uint32_t stride = 0;		// байты на элемент структурного буфера
	uint32_t usage = 0;			// BufferUsage
};

// Вид на буфер целиком или на его участок. Структурный — без формата (элементы по stride буфера), raw — байтовый
struct BufferViewDesc
{
	uint32_t firstElement = 0;
	uint32_t elementCount = 0;	// 0 — до конца буфера
	bool raw = false;
};

// Назначение текстуры — битовая маска
namespace TextureUsage
{
	enum : uint32_t
	{
		shaderResource = 1 << 0,
		renderTarget = 1 << 1,
		depthStencil = 1 << 2,
		unorderedAccess = 1 << 3,	// пишется compute-шейдером (StorageView на мип); мипы куба строит compute (cube_downsample.cs)
	};
}

struct TextureDesc
{
	uint32_t width = 1;
	uint32_t height = 1;
	uint32_t depth = 1;			// > 1 — объёмная текстура
	uint32_t arraySize = 1;		// срезов массива; у cubemap — 6
	uint32_t mipCount = 1;		// 0 — полная цепочка (после создания — настоящее число)
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	uint32_t usage = TextureUsage::shaderResource;
	bool cube = false;
};

// Данные одного подресурса при создании: подресурсы — по срезам, внутри среза — по мипам
struct TextureData
{
	const void* data = nullptr;
	uint32_t rowPitch = 0;
	uint32_t slicePitch = 0;
};

// Вид на текстуру: какой (automatic — по её описанию: массив, куб, объём или 2D), формат (UNKNOWN — формат текстуры;
// у typeless — обязателен), мипы и срезы. У цели рендера и UAV мип один — firstMip
struct TextureViewDesc
{
	enum class Kind { automatic, texture2D, texture2DArray, cube, texture3D };
	Kind kind = Kind::automatic;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	uint32_t firstMip = 0;
	uint32_t mipCount = 0;		// 0 — все с firstMip
	uint32_t firstSlice = 0;
	uint32_t sliceCount = 0;	// 0 — все с firstSlice
};

// Элемент раскладки вершин (как input element в D3D11 и D3D12)
struct VertexElement
{
	static constexpr uint32_t appendOffset = 0xFFFFFFFFu;	// сразу за предыдущим элементом потока

	const char* semantic = "";
	uint32_t semanticIndex = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	uint32_t slot = 0;						// поток вершин
	uint32_t offset = appendOffset;			// байты от начала вершины
	bool perInstance = false;
};

// Дескриптор в куче: CPU-адрес (создание вида, очистки), GPU-адрес (привязка, ImGui) и номер в куче — индекс для
// ResourceDescriptorHeap в шейдере. У куч без shader-visible (RTV, DSV, копии UAV для очисток) GPU-адреса нет
struct Descriptor
{
	static constexpr uint32_t invalidIndex = 0xFFFFFFFFu;

	D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
	uint32_t index = invalidIndex;

	bool valid() const { return cpu.ptr != 0; }
};

// Подресурсы вида — для барьеров по мипам и срезам (мипы куба неба, срезы каскадов теней)
struct SubresourceRange
{
	uint32_t firstMip = 0;
	uint32_t mipCount = 0;		// 0 — все
	uint32_t firstSlice = 0;
	uint32_t sliceCount = 0;	// 0 — все
};

// Освобождение дескрипторов и ресурсов — в DMD3D.cpp: отложенное, когда GPU закончил кадр, в котором их отпустили
// (кучи и аллокатор там); после DMD3D::destroy — сразу
void gpuFreeShaderDescriptor( const Descriptor& descriptor );
void gpuFreeStagingDescriptor( const Descriptor& descriptor );
void gpuFreeTargetDescriptor( const Descriptor& descriptor, bool depth );
void gpuReleaseResource( ID3D12Resource* resource, D3D12MA::Allocation* allocation );

// ---------------------------------------------------------------------------------------------------------------------
// Ресурсы
// ---------------------------------------------------------------------------------------------------------------------

class Buffer
{
public:
	Buffer() = default;
	Buffer( const Buffer& ) = delete;
	Buffer& operator=( const Buffer& ) = delete;
	Buffer( Buffer&& other ) noexcept { *this = std::move( other ); }
	Buffer& operator=( Buffer&& other ) noexcept
	{
		if( this != &other )
		{
			reset();
			m_resource = other.m_resource;
			m_allocation = other.m_allocation;
			other.m_resource = nullptr;
			other.m_allocation = nullptr;
			m_desc = other.m_desc;
			m_gpuAddress = other.m_gpuAddress;
			m_ringOffset = other.m_ringOffset;
			m_ringBytes = other.m_ringBytes;
			m_ringView = other.m_ringView;
			m_copyFence = other.m_copyFence;
		}
		return *this;
	}
	~Buffer() { reset(); }

	bool valid() const { return m_resource != nullptr || ring(); }
	const BufferDesc& desc() const { return m_desc; }
	uint32_t size() const { return m_desc.size; }
	// Буфер, который пишут каждый кадр (BufferUsage::cpuWrite): своего ресурса нет, данные — участок кольца DMD3D,
	// записанный в этом кадре (Device::updateResource*). Константы привязывает setConstantBuffer по адресу участка,
	// структурные и raw данные — setSRV( слот, буфер ) по временному дескриптору участка (ringView)
	bool ring() const { return ( m_desc.usage & BufferUsage::cpuWrite ) != 0; }
	bool ringConstant() const { return ring() && ( m_desc.usage & BufferUsage::constant ); }
	uint32_t ringOffset() const { return m_ringOffset; }
	uint32_t ringBytes() const { return m_ringBytes; }
	uint32_t ringView() const { return m_ringView; }
	void setRingSlice( uint32_t offset, uint32_t bytes )
	{
		m_ringOffset = offset;
		m_ringBytes = bytes;
		m_ringView = 0;
	}
	void setRingView( uint32_t descriptorIndex ) { m_ringView = descriptorIndex; }

	ID3D12Resource* handle() const { return m_resource; }
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddress() const { return m_gpuAddress; }
	// Значение fence кадра, в котором в буфер (readback) копировали: readBuffer читает, когда он пройден
	uint64_t copyFence() const { return m_copyFence; }
	void setCopyFence( uint64_t fence ) { m_copyFence = fence; }
	void reset( ID3D12Resource* resource = nullptr, D3D12MA::Allocation* allocation = nullptr, const BufferDesc& desc = {} )
	{
		gpuReleaseResource( m_resource, m_allocation );
		m_resource = resource;
		m_allocation = allocation;
		m_desc = desc;
		m_gpuAddress = resource ? resource->GetGPUVirtualAddress() : 0;
		m_ringOffset = m_ringBytes = m_ringView = 0;
		m_copyFence = 0;
	}

private:
	ID3D12Resource* m_resource = nullptr;
	D3D12MA::Allocation* m_allocation = nullptr;
	BufferDesc m_desc;
	D3D12_GPU_VIRTUAL_ADDRESS m_gpuAddress = 0;
	uint32_t m_ringOffset = 0;
	uint32_t m_ringBytes = 0;
	uint32_t m_ringView = 0;	// индекс временного SRV участка (только этот кадр)
	uint64_t m_copyFence = 0;
};

class Texture
{
public:
	Texture() = default;
	Texture( const Texture& ) = delete;
	Texture& operator=( const Texture& ) = delete;
	Texture( Texture&& other ) noexcept { *this = std::move( other ); }
	Texture& operator=( Texture&& other ) noexcept
	{
		if( this != &other )
		{
			reset();
			m_resource = other.m_resource;
			m_allocation = other.m_allocation;
			other.m_resource = nullptr;
			other.m_allocation = nullptr;
			m_desc = other.m_desc;
		}
		return *this;
	}
	~Texture() { reset(); }

	bool valid() const { return m_resource != nullptr; }
	const TextureDesc& desc() const { return m_desc; }
	uint32_t width() const { return m_desc.width; }
	uint32_t height() const { return m_desc.height; }
	uint32_t mipCount() const { return m_desc.mipCount; }

	ID3D12Resource* handle() const { return m_resource; }
	void reset( ID3D12Resource* resource = nullptr, D3D12MA::Allocation* allocation = nullptr, const TextureDesc& desc = {} )
	{
		gpuReleaseResource( m_resource, m_allocation );
		m_resource = resource;
		m_allocation = allocation;
		m_desc = desc;
	}

private:
	ID3D12Resource* m_resource = nullptr;
	D3D12MA::Allocation* m_allocation = nullptr;
	TextureDesc m_desc;
};

// Вид для чтения в шейдере (SRV): постоянный дескриптор в shader-visible куче, его номер — индекс в таблице привязок
// вызова (Shaders/bindless.sh). Вид помнит ресурс и подресурсы — по ним DMD3D ставит барьеры
class ShaderView
{
public:
	ShaderView() = default;
	ShaderView( const ShaderView& ) = delete;
	ShaderView& operator=( const ShaderView& ) = delete;
	ShaderView( ShaderView&& other ) noexcept { *this = std::move( other ); }
	ShaderView& operator=( ShaderView&& other ) noexcept
	{
		if( this != &other )
		{
			reset();
			m_descriptor = other.m_descriptor;
			m_resource = other.m_resource;
			m_range = other.m_range;
			other.m_descriptor = {};
			other.m_resource = nullptr;
		}
		return *this;
	}
	~ShaderView() { reset(); }

	bool valid() const { return m_descriptor.valid(); }
	const Descriptor& descriptor() const { return m_descriptor; }
	uint32_t index() const { return m_descriptor.index; }
	ID3D12Resource* resource() const { return m_resource; }
	const SubresourceRange& range() const { return m_range; }
	void reset()
	{
		if( m_descriptor.valid() )
			gpuFreeShaderDescriptor( m_descriptor );
		m_descriptor = {};
		m_resource = nullptr;
		m_range = {};
	}
	void reset( const Descriptor& descriptor, ID3D12Resource* resource, const SubresourceRange& range = {} )
	{
		reset();
		m_descriptor = descriptor;
		m_resource = resource;
		m_range = range;
	}

private:
	Descriptor m_descriptor;
	ID3D12Resource* m_resource = nullptr;
	SubresourceRange m_range;
};

// Вид для записи из compute (UAV): дескриптор в shader-visible куче и его копия в CPU-куче — ClearUnorderedAccessView
// требует оба
class StorageView
{
public:
	StorageView() = default;
	StorageView( const StorageView& ) = delete;
	StorageView& operator=( const StorageView& ) = delete;
	StorageView( StorageView&& other ) noexcept { *this = std::move( other ); }
	StorageView& operator=( StorageView&& other ) noexcept
	{
		if( this != &other )
		{
			reset();
			m_descriptor = other.m_descriptor;
			m_clearDescriptor = other.m_clearDescriptor;
			m_resource = other.m_resource;
			m_range = other.m_range;
			other.m_descriptor = {};
			other.m_clearDescriptor = {};
			other.m_resource = nullptr;
		}
		return *this;
	}
	~StorageView() { reset(); }

	bool valid() const { return m_descriptor.valid(); }
	const Descriptor& descriptor() const { return m_descriptor; }
	const Descriptor& clearDescriptor() const { return m_clearDescriptor; }
	uint32_t index() const { return m_descriptor.index; }
	ID3D12Resource* resource() const { return m_resource; }
	const SubresourceRange& range() const { return m_range; }
	void reset()
	{
		if( m_descriptor.valid() )
			gpuFreeShaderDescriptor( m_descriptor );
		if( m_clearDescriptor.valid() )
			gpuFreeStagingDescriptor( m_clearDescriptor );
		m_descriptor = {};
		m_clearDescriptor = {};
		m_resource = nullptr;
		m_range = {};
	}
	void reset( const Descriptor& descriptor, const Descriptor& clearDescriptor, ID3D12Resource* resource, const SubresourceRange& range = {} )
	{
		reset();
		m_descriptor = descriptor;
		m_clearDescriptor = clearDescriptor;
		m_resource = resource;
		m_range = range;
	}

private:
	Descriptor m_descriptor;
	Descriptor m_clearDescriptor;
	ID3D12Resource* m_resource = nullptr;
	SubresourceRange m_range;
};

// Цель рендера: цвет (RTV) или глубина (DSV) — по формату при создании, один мип
class TargetView
{
public:
	TargetView() = default;
	TargetView( const TargetView& ) = delete;
	TargetView& operator=( const TargetView& ) = delete;
	TargetView( TargetView&& other ) noexcept { *this = std::move( other ); }
	TargetView& operator=( TargetView&& other ) noexcept
	{
		if( this != &other )
		{
			reset();
			m_descriptor = other.m_descriptor;
			m_depth = other.m_depth;
			m_format = other.m_format;
			m_resource = other.m_resource;
			m_range = other.m_range;
			other.m_descriptor = {};
			other.m_resource = nullptr;
		}
		return *this;
	}
	~TargetView() { reset(); }

	bool valid() const { return m_descriptor.valid(); }
	bool isDepth() const { return m_depth; }
	D3D12_CPU_DESCRIPTOR_HANDLE handle() const { return m_descriptor.cpu; }
	DXGI_FORMAT format() const { return m_format; }
	ID3D12Resource* resource() const { return m_resource; }
	const SubresourceRange& range() const { return m_range; }
	void reset()
	{
		if( m_descriptor.valid() )
			gpuFreeTargetDescriptor( m_descriptor, m_depth );
		m_descriptor = {};
		m_resource = nullptr;
		m_format = DXGI_FORMAT_UNKNOWN;
		m_range = {};
	}
	void reset( const Descriptor& descriptor, bool depth, DXGI_FORMAT format, ID3D12Resource* resource, const SubresourceRange& range = {} )
	{
		reset();
		m_descriptor = descriptor;
		m_depth = depth;
		m_format = format;
		m_resource = resource;
		m_range = range;
	}

private:
	Descriptor m_descriptor;
	bool m_depth = false;
	DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
	ID3D12Resource* m_resource = nullptr;
	SubresourceRange m_range;
};

// Скомпилированная стадия шейдера — байткод; объект состояния из него собирает пайплайн (GpuPipeline.h)
class ShaderStage
{
public:
	bool valid() const { return !m_bytecode.empty(); }
	SRVType type() const { return m_type; }
	const void* data() const { return m_bytecode.data(); }
	size_t size() const { return m_bytecode.size(); }
	// FNV-1a байткода: имя пайплайна в ID3D12PipelineLibrary, ключ compute-пайплайна
	uint64_t hash() const { return m_hash; }
	void reset( SRVType type = SRVType::vs, const void* bytecode = nullptr, size_t size = 0 )
	{
		m_type = type;
		m_bytecode.assign( static_cast<const uint8_t*>( bytecode ), static_cast<const uint8_t*>( bytecode ) + ( bytecode ? size : 0 ) );
		m_hash = 14695981039346656037ull;
		for( uint8_t byte : m_bytecode )
		{
			m_hash ^= byte;
			m_hash *= 1099511628211ull;
		}
	}

private:
	SRVType m_type = SRVType::vs;
	std::vector<uint8_t> m_bytecode;
	uint64_t m_hash = 0;
};

// Раскладка вершин — часть пайплайна; байткода шейдера ей не нужно
class InputLayout
{
public:
	bool valid() const { return !m_elements.empty(); }
	const std::vector<VertexElement>& elements() const { return m_elements; }
	void reset( const std::vector<VertexElement>& elements = {} ) { m_elements = elements; }

private:
	std::vector<VertexElement> m_elements;
};
