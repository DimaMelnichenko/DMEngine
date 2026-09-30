#pragma once

// Непрозрачные ресурсы GPU и их виды — как ресурсы и дескрипторы в D3D12 (docs/d3d12_migration.md, шаг A2).
// Объекты сцены, материалы и проходы держат только их и создают через DMD3D по описаниям ниже — в них нет типов
// D3D11, а DXGI_FORMAT и D3D_PRIMITIVE_TOPOLOGY общие у D3D11 и D3D12. ID3D11* живёт внутри Graphics/D3D: методы
// handle() и reset() — для бэкенда (DMD3D, RenderTarget, CubeTarget, DMStructuredBuffer, TextureImages)
#include <d3d11.h>
#include <cstdint>
#include <vector>
#include "Utils\utilites.h"
#include "DM3DUtils.h"

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
		cpuWrite = 1 << 8,			// пишется с CPU каждый кадр (Device::updateResource); у констант — участок кольца
									// DMD3D (ConstantRing.h), у остальных — DYNAMIC-буфер; в D3D12 — upload-куча
		readback = 1 << 9,			// копия для чтения на CPU (DMD3D::copyBuffer + readBuffer; в D3D12 — readback-куча)
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
		unorderedAccess = 1 << 3,
		generateMips = 1 << 4,	// мипы строит DMD3D::generateMips (в D3D12 — compute-проход, шаг A6)
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

// ---------------------------------------------------------------------------------------------------------------------
// Ресурсы
// ---------------------------------------------------------------------------------------------------------------------

class Buffer
{
public:
	bool valid() const { return m_buffer != nullptr || ring(); }
	const BufferDesc& desc() const { return m_desc; }
	uint32_t size() const { return m_desc.size; }
	// Константы, которые пишут каждый кадр (BufferUsage::constant | cpuWrite): своего ресурса нет, данные — участок
	// кольца констант DMD3D, записанный в этом кадре (Device::updateResource*); setConstantBuffer привязывает его
	bool ring() const { return ( m_desc.usage & BufferUsage::constant ) && ( m_desc.usage & BufferUsage::cpuWrite ); }
	uint32_t ringOffset() const { return m_ringOffset; }
	uint32_t ringBytes() const { return m_ringBytes; }
	void setRingSlice( uint32_t offset, uint32_t bytes )
	{
		m_ringOffset = offset;
		m_ringBytes = bytes;
	}

	ID3D11Buffer* handle() const { return m_buffer.get(); }
	void reset( ID3D11Buffer* buffer = nullptr, const BufferDesc& desc = {} )
	{
		m_buffer.reset( buffer );
		m_desc = desc;
		m_ringOffset = m_ringBytes = 0;
	}

private:
	com_unique_ptr<ID3D11Buffer> m_buffer;
	BufferDesc m_desc;
	uint32_t m_ringOffset = 0;
	uint32_t m_ringBytes = 0;
};

class Texture
{
public:
	bool valid() const { return m_resource != nullptr; }
	const TextureDesc& desc() const { return m_desc; }
	uint32_t width() const { return m_desc.width; }
	uint32_t height() const { return m_desc.height; }
	uint32_t mipCount() const { return m_desc.mipCount; }

	ID3D11Resource* handle() const { return m_resource.get(); }
	void reset( ID3D11Resource* resource = nullptr, const TextureDesc& desc = {} )
	{
		m_resource.reset( resource );
		m_desc = desc;
	}

private:
	com_unique_ptr<ID3D11Resource> m_resource;
	TextureDesc m_desc;
};

// Вид для чтения в шейдере (SRV). Виды помнят свой ресурс: по нему DMD3D::beginPass снимает со входов виды того,
// во что проход пишет (в D3D12 — состояние ресурса для барьеров)
class ShaderView
{
public:
	bool valid() const { return m_view != nullptr; }

	ID3D11ShaderResourceView* handle() const { return m_view.get(); }
	ID3D11Resource* resource() const { return m_resource; }
	void reset( ID3D11ShaderResourceView* view = nullptr, ID3D11Resource* resource = nullptr )
	{
		m_view.reset( view );
		m_resource = resource;
	}

private:
	com_unique_ptr<ID3D11ShaderResourceView> m_view;
	ID3D11Resource* m_resource = nullptr;
};

// Вид для записи из compute-шейдера (UAV)
class StorageView
{
public:
	bool valid() const { return m_view != nullptr; }

	ID3D11UnorderedAccessView* handle() const { return m_view.get(); }
	ID3D11Resource* resource() const { return m_resource; }
	void reset( ID3D11UnorderedAccessView* view = nullptr, ID3D11Resource* resource = nullptr )
	{
		m_view.reset( view );
		m_resource = resource;
	}

private:
	com_unique_ptr<ID3D11UnorderedAccessView> m_view;
	ID3D11Resource* m_resource = nullptr;
};

// Цель рендера: цвет (RTV) или глубина (DSV) — по формату текстуры
class TargetView
{
public:
	bool valid() const { return m_color != nullptr || m_depth != nullptr; }
	bool isDepth() const { return m_depth != nullptr; }

	ID3D11RenderTargetView* color() const { return m_color.get(); }
	ID3D11DepthStencilView* depth() const { return m_depth.get(); }
	ID3D11Resource* resource() const { return m_resource; }
	void reset( ID3D11RenderTargetView* color, ID3D11Resource* resource = nullptr )
	{
		m_color.reset( color );
		m_depth.reset();
		m_resource = resource;
	}
	void reset( ID3D11DepthStencilView* depth, ID3D11Resource* resource = nullptr )
	{
		m_depth.reset( depth );
		m_color.reset();
		m_resource = resource;
	}
	void reset()
	{
		m_color.reset();
		m_depth.reset();
		m_resource = nullptr;
	}

private:
	com_unique_ptr<ID3D11RenderTargetView> m_color;
	com_unique_ptr<ID3D11DepthStencilView> m_depth;
	ID3D11Resource* m_resource = nullptr;
};

// Скомпилированный шейдер одной стадии (в D3D12 станет частью PSO, шаг A4)
class ShaderStage
{
public:
	bool valid() const { return m_shader != nullptr; }
	SRVType type() const { return m_type; }

	ID3D11DeviceChild* handle() const { return m_shader.get(); }
	void reset( SRVType type, ID3D11DeviceChild* shader )
	{
		m_type = type;
		m_shader.reset( shader );
	}

private:
	SRVType m_type = SRVType::vs;
	com_unique_ptr<ID3D11DeviceChild> m_shader;
};

// Раскладка вершин, собранная под байткод вершинного шейдера
class InputLayout
{
public:
	bool valid() const { return m_layout != nullptr; }

	ID3D11InputLayout* handle() const { return m_layout.get(); }
	void reset( ID3D11InputLayout* layout = nullptr ) { m_layout.reset( layout ); }

private:
	com_unique_ptr<ID3D11InputLayout> m_layout;
};
