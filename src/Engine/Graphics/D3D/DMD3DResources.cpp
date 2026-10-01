// DMD3D: ресурсы и виды по описаниям GpuResources.h (память — D3D12MA), загрузка данных, копии и чтение на CPU,
// запись в кольцо данных кадра, имена ресурсов
#include "DMD3D.h"
#include "Logger\Logger.h"
#include <D3D12MemAlloc.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{

bool isDepthFormat( DXGI_FORMAT format )
{
	return format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT || format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
		   format == DXGI_FORMAT_D16_UNORM;
}

// Формат глубины для typeless-текстуры (карта теней — R32_TYPELESS: DSV D32_FLOAT, SRV R32_FLOAT)
DXGI_FORMAT depthFormatFor( DXGI_FORMAT format )
{
	switch( format )
	{
		case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_D32_FLOAT;
		case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_D16_UNORM;
		default: return format;
	}
}

TextureViewDesc::Kind resolveKind( const TextureDesc& texture, const TextureViewDesc& view )
{
	if( view.kind != TextureViewDesc::Kind::automatic )
		return view.kind;
	if( texture.depth > 1 )
		return TextureViewDesc::Kind::texture3D;
	if( texture.cube )
		return TextureViewDesc::Kind::cube;
	return texture.arraySize > 1 ? TextureViewDesc::Kind::texture2DArray : TextureViewDesc::Kind::texture2D;
}

uint32_t subresourceCount( const TextureDesc& desc )
{
	return desc.mipCount * ( desc.depth > 1 ? 1 : desc.arraySize );
}

std::wstring toWide( const std::string& text )
{
	if( text.empty() )
		return {};
	const int length = MultiByteToWideChar( CP_UTF8, 0, text.c_str(), static_cast<int>( text.size() ), nullptr, 0 );
	std::wstring wide( static_cast<size_t>( std::max( length, 0 ) ), L' ' );
	if( length > 0 )
		MultiByteToWideChar( CP_UTF8, 0, text.c_str(), static_cast<int>( text.size() ), wide.data(), length );
	return wide;
}

}

bool DMD3D::createResource( const D3D12_RESOURCE_DESC1& desc, D3D12_HEAP_TYPE heap, D3D12_BARRIER_LAYOUT initialLayout,
							const D3D12_CLEAR_VALUE* clearValue, ID3D12Resource** resource, D3D12MA::Allocation** allocation )
{
	D3D12MA::ALLOCATION_DESC allocationDesc = {};
	allocationDesc.HeapType = heap;
	const HRESULT hr = m_allocator->CreateResource3( &allocationDesc, &desc, initialLayout, clearValue, 0, nullptr, allocation,
													 __uuidof( ID3D12Resource ), reinterpret_cast<void**>( resource ) );
	if( FAILED( hr ) )
	{
		LOG( "CreateResource failed, HRESULT " + std::to_string( static_cast<long>( hr ) ) + ": " +
			 ( desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? "buffer of " + std::to_string( desc.Width ) + " bytes" :
			   "texture " + std::to_string( desc.Width ) + "x" + std::to_string( desc.Height ) + ", format " + std::to_string( desc.Format ) ) );
		return false;
	}
	ResourceState state;
	state.texture = desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER;
	if( state.texture )
	{
		const D3D12_RESOURCE_DESC created = ( *resource )->GetDesc();
		state.mipCount = std::max<uint32_t>( created.MipLevels, 1 );
		state.arraySize = created.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : std::max<uint32_t>( created.DepthOrArraySize, 1 );
	}
	SubresourceState initial;
	initial.layout = initialLayout;
	state.subresources.assign( static_cast<size_t>( state.mipCount ) * state.arraySize, initial );
	m_states[*resource] = std::move( state );
	return true;
}

bool DMD3D::createStaging( uint64_t bytes, D3D12_HEAP_TYPE heap, ID3D12Resource** resource, D3D12MA::Allocation** allocation, void** mapped )
{
	D3D12_RESOURCE_DESC1 desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = std::max<uint64_t>( bytes, 1 );
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if( !createResource( desc, heap, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, resource, allocation ) )
		return false;
	( *resource )->SetName( heap == D3D12_HEAP_TYPE_UPLOAD ? L"Upload staging" : L"Readback staging" );
	if( mapped )
	{
		const D3D12_RANGE noRead = { 0, 0 };
		if( FAILED( ( *resource )->Map( 0, heap == D3D12_HEAP_TYPE_UPLOAD ? &noRead : nullptr, mapped ) ) )
			return false;
	}
	return true;
}

bool DMD3D::uploadToBuffer( ID3D12Resource* destination, uint64_t destinationOffset, const void* data, uint64_t size )
{
	if( !data || size == 0 )
		return true;
	ensureRecording();
	barrier( destination, D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, true );

	// В кадре небольшие данные идут через участок кольца (он живёт, пока GPU не закончит кадр); до первого кадра и
	// большие — отдельный upload-буфер, отпускается, когда GPU закончит копирование
	uint32_t offset = 0, bytes = 0;
	if( m_frameStarted && !m_writingBuffer && size <= constantRingBytes / frameCount / 4 )
	{
		if( void* slice = m_constantRing.beginWrite( static_cast<uint32_t>( size ), offset, bytes ) )
		{
			memcpy( slice, data, size );
			m_constantRing.finishWrite();
			flushBarriers();
			m_commandList->CopyBufferRegion( destination, destinationOffset, m_constantRing.handle(), offset, size );
			return true;
		}
	}
	ID3D12Resource* staging = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	void* mapped = nullptr;
	if( !createStaging( size, D3D12_HEAP_TYPE_UPLOAD, &staging, &allocation, &mapped ) )
		return false;
	memcpy( mapped, data, size );
	staging->Unmap( 0, nullptr );
	flushBarriers();
	m_commandList->CopyBufferRegion( destination, destinationOffset, staging, 0, size );
	m_states.erase( staging );
	deferRelease( staging );
	deferRelease( allocation );
	return true;
}

bool DMD3D::uploadToTexture( ID3D12Resource* destination, const TextureDesc& desc, const TextureData* initial )
{
	ensureRecording();
	const uint32_t count = subresourceCount( desc );
	const D3D12_RESOURCE_DESC resourceDesc = destination->GetDesc();
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts( count );
	std::vector<UINT> rows( count );
	std::vector<UINT64> rowBytes( count );
	UINT64 total = 0;
	m_device->GetCopyableFootprints( &resourceDesc, 0, count, 0, layouts.data(), rows.data(), rowBytes.data(), &total );

	ID3D12Resource* staging = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	void* mapped = nullptr;
	if( !createStaging( total, D3D12_HEAP_TYPE_UPLOAD, &staging, &allocation, &mapped ) )
		return false;

	// Подресурс i у D3D12 — мип + срез × мипов: тот же порядок, что у TextureData (срез за срезом, внутри среза мипы)
	for( uint32_t i = 0; i < count; ++i )
	{
		const TextureData& source = initial[i];
		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout = layouts[i];
		const uint32_t sourceRowPitch = source.rowPitch ? source.rowPitch : static_cast<uint32_t>( rowBytes[i] );
		const uint32_t sourceSlicePitch = source.slicePitch ? source.slicePitch : sourceRowPitch * rows[i];
		for( uint32_t z = 0; z < layout.Footprint.Depth; ++z )
		{
			for( uint32_t row = 0; row < rows[i]; ++row )
			{
				uint8_t* target = static_cast<uint8_t*>( mapped ) + layout.Offset +
								  ( static_cast<uint64_t>( z ) * rows[i] + row ) * layout.Footprint.RowPitch;
				const uint8_t* pixels = static_cast<const uint8_t*>( source.data ) + static_cast<uint64_t>( z ) * sourceSlicePitch +
										static_cast<uint64_t>( row ) * sourceRowPitch;
				memcpy( target, pixels, rowBytes[i] );
			}
		}
	}
	staging->Unmap( 0, nullptr );

	barrier( destination, D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_COPY_DEST );
	flushBarriers();
	for( uint32_t i = 0; i < count; ++i )
	{
		D3D12_TEXTURE_COPY_LOCATION target = {};
		target.pResource = destination;
		target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		target.SubresourceIndex = i;
		D3D12_TEXTURE_COPY_LOCATION source = {};
		source.pResource = staging;
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = layouts[i];
		m_commandList->CopyTextureRegion( &target, 0, 0, 0, &source, nullptr );
	}
	// Дальше текстуру читают шейдеры (и GUI)
	barrier( destination, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE );
	m_states.erase( staging );
	deferRelease( staging );
	deferRelease( allocation );
	return true;
}

bool DMD3D::createBuffer( const BufferDesc& desc, const void* initialData, Buffer& buffer )
{
	// Константы кадра — участок кольца, своего ресурса нет: данные появятся при первой записи
	buffer.reset( nullptr, nullptr, desc );
	if( buffer.ring() )
		return true;

	D3D12_RESOURCE_DESC1 resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	// CBV — участками по 256 байт
	resourceDesc.Width = ( desc.usage & BufferUsage::constant ) ? ( desc.size + 255 ) & ~255u : std::max( desc.size, 1u );
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = ( desc.usage & BufferUsage::unorderedAccess ) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
	const bool readback = ( desc.usage & BufferUsage::readback ) != 0;

	ID3D12Resource* resource = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	if( !createResource( resourceDesc, readback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr,
						 &resource, &allocation ) )
		return false;
	buffer.reset( resource, allocation, desc );
	if( initialData && !readback )
	{
		if( !uploadToBuffer( resource, 0, initialData, desc.size ) )
			return false;
		barrier( resource, steadyAccess( desc.usage ) );
	}
	return true;
}

bool DMD3D::createShaderConstantBuffer( size_t byteSize, Buffer& buffer )
{
	BufferDesc desc;
	desc.size = static_cast<uint32_t>( byteSize );
	desc.usage = BufferUsage::constant | BufferUsage::cpuWrite;
	return createBuffer( desc, nullptr, buffer );
}

bool DMD3D::createVertexBuffer( Buffer& buffer, const void* data, size_t sizeInBytes )
{
	BufferDesc desc;
	desc.size = static_cast<uint32_t>( sizeInBytes );
	desc.usage = BufferUsage::vertex;
	return createBuffer( desc, data, buffer );
}

bool DMD3D::createIndexBuffer( Buffer& buffer, const void* data, size_t sizeInBytes )
{
	BufferDesc desc;
	desc.size = static_cast<uint32_t>( sizeInBytes );
	desc.usage = BufferUsage::index;
	return createBuffer( desc, data, buffer );
}

bool DMD3D::createShaderView( const Buffer& buffer, const BufferViewDesc& desc, ShaderView& view )
{
	view.reset();
	if( buffer.ring() )
	{
		LOG( "Shader view of a per-frame buffer (BufferUsage::cpuWrite) is not created: bind the buffer itself with setSRV" );
		return false;
	}
	if( !buffer.handle() )
		return false;
	const bool structured = ( buffer.desc().usage & BufferUsage::structured ) != 0 && buffer.desc().stride;
	const uint32_t elementSize = desc.raw || !structured ? 4 : buffer.desc().stride;
	D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.raw || !structured ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	viewDesc.Buffer.FirstElement = desc.firstElement;
	viewDesc.Buffer.NumElements = desc.elementCount ? desc.elementCount : buffer.size() / elementSize - desc.firstElement;
	viewDesc.Buffer.StructureByteStride = desc.raw || !structured ? 0 : buffer.desc().stride;
	viewDesc.Buffer.Flags = desc.raw || !structured ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
	const Descriptor descriptor = allocateShaderDescriptor();
	if( !descriptor.valid() )
		return false;
	m_device->CreateShaderResourceView( buffer.handle(), &viewDesc, descriptor.cpu );
	view.reset( descriptor, buffer.handle() );
	return true;
}

bool DMD3D::createStorageView( const Buffer& buffer, const BufferViewDesc& desc, StorageView& view )
{
	view.reset();
	if( !buffer.handle() )
		return false;
	const bool structured = ( buffer.desc().usage & BufferUsage::structured ) != 0 && buffer.desc().stride;
	const uint32_t elementSize = desc.raw || !structured ? 4 : buffer.desc().stride;
	D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.raw || !structured ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	viewDesc.Buffer.FirstElement = desc.firstElement;
	viewDesc.Buffer.NumElements = desc.elementCount ? desc.elementCount : buffer.size() / elementSize - desc.firstElement;
	viewDesc.Buffer.StructureByteStride = desc.raw || !structured ? 0 : buffer.desc().stride;
	viewDesc.Buffer.Flags = desc.raw || !structured ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
	const Descriptor descriptor = allocateShaderDescriptor();
	const Descriptor clearDescriptor = m_stagingHeap.allocate();
	if( !descriptor.valid() || !clearDescriptor.valid() )
	{
		LOG( "Descriptor heap is full (UAV)" );
		return false;
	}
	m_device->CreateUnorderedAccessView( buffer.handle(), nullptr, &viewDesc, descriptor.cpu );
	m_device->CreateUnorderedAccessView( buffer.handle(), nullptr, &viewDesc, clearDescriptor.cpu );
	view.reset( descriptor, clearDescriptor, buffer.handle() );
	return true;
}

bool DMD3D::createTexture( const TextureDesc& desc, const TextureData* initial, Texture& texture )
{
	texture.reset();
	D3D12_RESOURCE_DESC1 resourceDesc = {};
	resourceDesc.Dimension = desc.depth > 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = desc.width;
	resourceDesc.Height = desc.height;
	resourceDesc.DepthOrArraySize = static_cast<UINT16>( desc.depth > 1 ? desc.depth : desc.arraySize );
	resourceDesc.MipLevels = static_cast<UINT16>( desc.mipCount );	// 0 — полная цепочка
	resourceDesc.Format = desc.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	if( desc.usage & TextureUsage::renderTarget )
		resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if( desc.usage & TextureUsage::depthStencil )
		resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	if( desc.usage & TextureUsage::unorderedAccess )
		resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if( ( desc.usage & TextureUsage::depthStencil ) && !( desc.usage & TextureUsage::shaderResource ) )
		resourceDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

	// Начальный layout — по назначению; текстуры с данными — COMMON (первый барьер — в COPY_DEST). Optimized clear
	// value: у глубины — 0 (обратная глубина), у цветной цели — цвет очистки описания, если задан (буфер сцены)
	D3D12_BARRIER_LAYOUT layout = D3D12_BARRIER_LAYOUT_COMMON;
	D3D12_CLEAR_VALUE clearValue = {};
	const D3D12_CLEAR_VALUE* clear = nullptr;
	if( desc.usage & TextureUsage::depthStencil )
	{
		layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
		clearValue.Format = depthFormatFor( desc.format );
		clearValue.DepthStencil.Depth = 0.0f;
		clear = &clearValue;
	}
	else if( desc.usage & TextureUsage::renderTarget )
	{
		layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
		if( desc.hasClearColor )
		{
			clearValue.Format = desc.format;
			memcpy( clearValue.Color, desc.clearColor, sizeof( clearValue.Color ) );
			clear = &clearValue;
		}
	}
	else if( desc.usage & TextureUsage::unorderedAccess )
		layout = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;

	ID3D12Resource* resource = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	if( !createResource( resourceDesc, D3D12_HEAP_TYPE_DEFAULT, layout, clear, &resource, &allocation ) )
		return false;

	TextureDesc created = desc;
	created.mipCount = resource->GetDesc().MipLevels;
	texture.reset( resource, allocation, created );
	if( initial )
		return uploadToTexture( resource, created, initial );
	if( desc.usage & ( TextureUsage::renderTarget | TextureUsage::depthStencil ) )
	{
		// Память под целью из кучи без обнуления (D3D12MA): первой операцией над ней должна быть очистка, копия или
		// discard, иначе рисование в неё без очистки (таблицы неба, уровни bloom) — ошибка. Discard — в начальном layout
		ensureRecording();
		flushBarriers();
		m_commandList->DiscardResource( resource, nullptr );
		// Discard — доступ к цели: следующий барьер ждёт его (SyncBefore NONE после доступа — ошибка), а проход в том же
		// layout и доступе барьера не требует
		const bool depthTarget = ( desc.usage & TextureUsage::depthStencil ) != 0;
		for( SubresourceState& sub : m_states[resource].subresources )
		{
			sub.sync = depthTarget ? D3D12_BARRIER_SYNC_DEPTH_STENCIL : D3D12_BARRIER_SYNC_RENDER_TARGET;
			sub.access = depthTarget ? D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE : D3D12_BARRIER_ACCESS_RENDER_TARGET;
		}
	}
	return true;
}

bool DMD3D::createShaderView( const Texture& texture, const TextureViewDesc& desc, ShaderView& view )
{
	view.reset();
	if( !texture.handle() )
		return false;
	const TextureDesc& textureDesc = texture.desc();
	const TextureViewDesc::Kind kind = resolveKind( textureDesc, desc );
	const uint32_t mipCount = desc.mipCount ? desc.mipCount : textureDesc.mipCount - desc.firstMip;
	const uint32_t sliceCount = desc.sliceCount ? desc.sliceCount : textureDesc.arraySize - desc.firstSlice;
	D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.format != DXGI_FORMAT_UNKNOWN ? desc.format : textureDesc.format;
	viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	switch( kind )
	{
		case TextureViewDesc::Kind::texture3D:
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			viewDesc.Texture3D.MostDetailedMip = desc.firstMip;
			viewDesc.Texture3D.MipLevels = mipCount;
			break;
		case TextureViewDesc::Kind::cube:
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			viewDesc.TextureCube.MostDetailedMip = desc.firstMip;
			viewDesc.TextureCube.MipLevels = mipCount;
			break;
		case TextureViewDesc::Kind::texture2DArray:
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			viewDesc.Texture2DArray.MostDetailedMip = desc.firstMip;
			viewDesc.Texture2DArray.MipLevels = mipCount;
			viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
			viewDesc.Texture2DArray.ArraySize = sliceCount;
			break;
		default:
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			viewDesc.Texture2D.MostDetailedMip = desc.firstMip;
			viewDesc.Texture2D.MipLevels = mipCount;
			break;
	}
	const Descriptor descriptor = allocateShaderDescriptor();
	if( !descriptor.valid() )
		return false;
	m_device->CreateShaderResourceView( texture.handle(), &viewDesc, descriptor.cpu );
	// Куб: барьеры — по всем шести граням, вид на них один
	const uint32_t rangeSlices = kind == TextureViewDesc::Kind::cube ? textureDesc.arraySize - desc.firstSlice : sliceCount;
	view.reset( descriptor, texture.handle(), { desc.firstMip, mipCount, desc.firstSlice, rangeSlices } );
	return true;
}

bool DMD3D::createTargetView( const Texture& texture, const TextureViewDesc& desc, TargetView& view )
{
	view.reset();
	if( !texture.handle() )
		return false;
	const TextureDesc& textureDesc = texture.desc();
	const TextureViewDesc::Kind kind = resolveKind( textureDesc, desc );
	const uint32_t sliceCount = desc.sliceCount ? desc.sliceCount : textureDesc.arraySize - desc.firstSlice;
	DXGI_FORMAT format = desc.format != DXGI_FORMAT_UNKNOWN ? desc.format : textureDesc.format;
	if( textureDesc.usage & TextureUsage::depthStencil )
		format = depthFormatFor( format );

	if( isDepthFormat( format ) )
	{
		const Descriptor descriptor = m_dsvHeap.allocate();
		if( !descriptor.valid() )
		{
			LOG( "DSV descriptor heap is full" );
			return false;
		}
		D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
		viewDesc.Format = format;
		if( kind == TextureViewDesc::Kind::texture2DArray || kind == TextureViewDesc::Kind::cube )
		{
			viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			viewDesc.Texture2DArray.MipSlice = desc.firstMip;
			viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
			viewDesc.Texture2DArray.ArraySize = sliceCount;
		}
		else
		{
			viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			viewDesc.Texture2D.MipSlice = desc.firstMip;
		}
		m_device->CreateDepthStencilView( texture.handle(), &viewDesc, descriptor.cpu );
		view.reset( descriptor, true, format, texture.handle(), { desc.firstMip, 1, desc.firstSlice, sliceCount } );
		return true;
	}

	const Descriptor descriptor = m_rtvHeap.allocate();
	if( !descriptor.valid() )
	{
		LOG( "RTV descriptor heap is full" );
		return false;
	}
	D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
	viewDesc.Format = format;
	if( kind == TextureViewDesc::Kind::texture3D )
	{
		viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
		viewDesc.Texture3D.MipSlice = desc.firstMip;
		viewDesc.Texture3D.FirstWSlice = desc.firstSlice;
		viewDesc.Texture3D.WSize = desc.sliceCount ? desc.sliceCount : static_cast<UINT>( -1 );
	}
	else if( kind == TextureViewDesc::Kind::texture2DArray || kind == TextureViewDesc::Kind::cube )
	{
		viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		viewDesc.Texture2DArray.MipSlice = desc.firstMip;
		viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
		viewDesc.Texture2DArray.ArraySize = sliceCount;
	}
	else
	{
		viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = desc.firstMip;
	}
	m_device->CreateRenderTargetView( texture.handle(), &viewDesc, descriptor.cpu );
	view.reset( descriptor, false, format, texture.handle(), { desc.firstMip, 1, desc.firstSlice, kind == TextureViewDesc::Kind::texture3D ? 1 : sliceCount } );
	return true;
}

bool DMD3D::createStorageView( const Texture& texture, const TextureViewDesc& desc, StorageView& view )
{
	view.reset();
	if( !texture.handle() )
		return false;
	const TextureDesc& textureDesc = texture.desc();
	const TextureViewDesc::Kind kind = resolveKind( textureDesc, desc );
	const uint32_t sliceCount = desc.sliceCount ? desc.sliceCount : textureDesc.arraySize - desc.firstSlice;
	D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.format != DXGI_FORMAT_UNKNOWN ? desc.format : textureDesc.format;
	if( kind == TextureViewDesc::Kind::texture3D )
	{
		viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
		viewDesc.Texture3D.MipSlice = desc.firstMip;
		viewDesc.Texture3D.FirstWSlice = desc.firstSlice;
		viewDesc.Texture3D.WSize = desc.sliceCount ? desc.sliceCount : static_cast<UINT>( -1 );
	}
	else if( kind == TextureViewDesc::Kind::texture2DArray || kind == TextureViewDesc::Kind::cube )
	{
		viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		viewDesc.Texture2DArray.MipSlice = desc.firstMip;
		viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
		viewDesc.Texture2DArray.ArraySize = sliceCount;
	}
	else
	{
		viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = desc.firstMip;
	}
	const Descriptor descriptor = allocateShaderDescriptor();
	const Descriptor clearDescriptor = m_stagingHeap.allocate();
	if( !descriptor.valid() || !clearDescriptor.valid() )
	{
		LOG( "Descriptor heap is full (texture UAV)" );
		return false;
	}
	m_device->CreateUnorderedAccessView( texture.handle(), nullptr, &viewDesc, descriptor.cpu );
	m_device->CreateUnorderedAccessView( texture.handle(), nullptr, &viewDesc, clearDescriptor.cpu );
	view.reset( descriptor, clearDescriptor, texture.handle(), { desc.firstMip, 1, desc.firstSlice, kind == TextureViewDesc::Kind::texture3D ? 1 : sliceCount } );
	return true;
}

bool DMD3D::createShaderStage( SRVType type, const void* bytecode, size_t size, ShaderStage& stage )
{
	if( !bytecode || size == 0 )
		return false;
	stage.reset( type, bytecode, size );
	return true;
}

bool DMD3D::createInputLayout( const std::vector<VertexElement>& elements, const void*, size_t, InputLayout& layout )
{
	layout.reset( elements );
	return true;
}

void DMD3D::updateBuffer( Buffer& buffer, const void* data, size_t size )
{
	if( !buffer.handle() || !data )
		return;
	if( uploadToBuffer( buffer.handle(), 0, data, std::min<size_t>( size, buffer.size() ) ) )
		barrier( buffer.handle(), steadyAccess( buffer.desc().usage ) );
}

void DMD3D::copyBuffer( Buffer& destination, const Buffer& source )
{
	if( !destination.handle() || !source.handle() )
		return;
	barrier( source.handle(), D3D12_BARRIER_ACCESS_COPY_SOURCE );
	// Копия за копией в тот же буфер (readback каждый кадр) — барьер и при том же доступе: порядок записей
	barrier( destination.handle(), D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, true );
	flushBarriers();
	m_commandList->CopyBufferRegion( destination.handle(), 0, source.handle(), 0, std::min( destination.size(), source.size() ) );
	if( destination.desc().usage & BufferUsage::readback )
		destination.setCopyFence( m_fenceValue + 1 );	// сигнал в конце этого кадра (endFrame) или waitForGpu
	else
		barrier( destination.handle(), steadyAccess( destination.desc().usage ) );
}

bool DMD3D::readBuffer( const Buffer& readback, void* data, size_t size )
{
	if( !readback.handle() || readback.copyFence() == 0 || m_fence->GetCompletedValue() < readback.copyFence() )
		return false;
	const D3D12_RANGE range = { 0, std::min<SIZE_T>( size, readback.size() ) };
	void* mapped = nullptr;
	if( FAILED( readback.handle()->Map( 0, &range, &mapped ) ) )
		return false;
	memcpy( data, mapped, range.End );
	const D3D12_RANGE noWrite = { 0, 0 };
	readback.handle()->Unmap( 0, &noWrite );
	return true;
}

bool DMD3D::captureTexture( const Texture& texture, std::vector<SubresourceCopy>& copies, std::vector<uint8_t>& bytes )
{
	if( !texture.handle() )
		return false;
	ensureRecording();
	const uint32_t count = subresourceCount( texture.desc() );
	const D3D12_RESOURCE_DESC resourceDesc = texture.handle()->GetDesc();
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts( count );
	std::vector<UINT> rows( count );
	std::vector<UINT64> rowBytes( count );
	UINT64 total = 0;
	m_device->GetCopyableFootprints( &resourceDesc, 0, count, 0, layouts.data(), rows.data(), rowBytes.data(), &total );

	ID3D12Resource* readback = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	if( !createStaging( total, D3D12_HEAP_TYPE_READBACK, &readback, &allocation, nullptr ) )
		return false;

	barrier( texture.handle(), D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_LAYOUT_COPY_SOURCE );
	flushBarriers();
	for( uint32_t i = 0; i < count; ++i )
	{
		D3D12_TEXTURE_COPY_LOCATION target = {};
		target.pResource = readback;
		target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		target.PlacedFootprint = layouts[i];
		D3D12_TEXTURE_COPY_LOCATION source = {};
		source.pResource = texture.handle();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		source.SubresourceIndex = i;
		m_commandList->CopyTextureRegion( &target, 0, 0, 0, &source, nullptr );
	}
	barrier( texture.handle(), D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE );
	waitForGpu();

	bool ok = false;
	void* mapped = nullptr;
	const D3D12_RANGE range = { 0, static_cast<SIZE_T>( total ) };
	if( SUCCEEDED( readback->Map( 0, &range, &mapped ) ) )
	{
		bytes.assign( static_cast<const uint8_t*>( mapped ), static_cast<const uint8_t*>( mapped ) + total );
		const D3D12_RANGE noWrite = { 0, 0 };
		readback->Unmap( 0, &noWrite );
		copies.resize( count );
		for( uint32_t i = 0; i < count; ++i )
			copies[i] = { layouts[i].Offset, layouts[i].Footprint.RowPitch, rows[i], rowBytes[i] };
		ok = true;
	}
	m_states.erase( readback );
	readback->Release();
	allocation->Release();
	return ok;
}

bool DMD3D::captureBackBuffer( std::vector<uint8_t>& bytes, uint32_t& rowPitch )
{
	ID3D12Resource* backBuffer = m_backBuffers[m_backBufferIndex].get();
	const D3D12_RESOURCE_DESC resourceDesc = backBuffer->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
	UINT rows = 0;
	UINT64 rowBytes = 0, total = 0;
	m_device->GetCopyableFootprints( &resourceDesc, 0, 1, 0, &layout, &rows, &rowBytes, &total );

	ID3D12Resource* readback = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	if( !createStaging( total, D3D12_HEAP_TYPE_READBACK, &readback, &allocation, nullptr ) )
		return false;

	// Задний буфер в кадре — цель (beginFrame); на копию и обратно — свои барьеры
	flushBarriers();
	D3D12_TEXTURE_BARRIER toCopy = {};
	toCopy.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
	toCopy.SyncAfter = D3D12_BARRIER_SYNC_COPY;
	toCopy.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
	toCopy.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
	toCopy.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
	toCopy.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
	toCopy.pResource = backBuffer;
	toCopy.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFFu;
	D3D12_BARRIER_GROUP group = {};
	group.Type = D3D12_BARRIER_TYPE_TEXTURE;
	group.NumBarriers = 1;
	group.pTextureBarriers = &toCopy;
	m_commandList->Barrier( 1, &group );

	D3D12_TEXTURE_COPY_LOCATION target = {};
	target.pResource = readback;
	target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	target.PlacedFootprint = layout;
	D3D12_TEXTURE_COPY_LOCATION source = {};
	source.pResource = backBuffer;
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	source.SubresourceIndex = 0;
	m_commandList->CopyTextureRegion( &target, 0, 0, 0, &source, nullptr );

	D3D12_TEXTURE_BARRIER back = toCopy;
	back.SyncBefore = D3D12_BARRIER_SYNC_COPY;
	back.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
	back.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
	back.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
	back.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
	back.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
	group.pTextureBarriers = &back;
	m_commandList->Barrier( 1, &group );
	waitForGpu();

	bool ok = false;
	void* mapped = nullptr;
	const D3D12_RANGE range = { 0, static_cast<SIZE_T>( total ) };
	if( SUCCEEDED( readback->Map( 0, &range, &mapped ) ) )
	{
		bytes.assign( static_cast<const uint8_t*>( mapped ), static_cast<const uint8_t*>( mapped ) + total );
		const D3D12_RANGE noWrite = { 0, 0 };
		readback->Unmap( 0, &noWrite );
		rowPitch = layout.Footprint.RowPitch;
		ok = true;
	}
	m_states.erase( readback );
	readback->Release();
	allocation->Release();
	return ok;
}

void* DMD3D::beginWrite( Buffer& buffer, uint32_t size )
{
	if( m_writingBuffer || size == 0 )
		return nullptr;
	if( !buffer.ring() && !buffer.handle() )
		return nullptr;
	// Константы — участки по 256 байт (CBV); структурные — по шагу элемента (FirstElement вида), не меньше 16
	uint32_t alignment = ConstantRing::constantAlignment;
	if( buffer.ring() && !buffer.ringConstant() )
	{
		const uint32_t stride = buffer.desc().stride ? buffer.desc().stride : 4;
		alignment = stride;
		while( alignment % 16 != 0 )
			alignment += stride;
	}
	uint32_t offset = 0, bytes = 0;
	void* data = m_constantRing.beginWrite( size, offset, bytes, alignment );
	if( !data )
		return nullptr;
	if( buffer.ring() )
		buffer.setRingSlice( offset, bytes );
	m_writingBuffer = &buffer;
	m_writeOffset = offset;
	m_writeBytes = std::min( size, buffer.size() );
	return data;
}

void DMD3D::endWrite()
{
	if( !m_writingBuffer )
		return;
	Buffer& buffer = *m_writingBuffer;
	m_writingBuffer = nullptr;
	m_constantRing.finishWrite();
	if( buffer.ringConstant() )
		return;
	if( buffer.ring() )
	{
		// Структурные данные кадра: временный SRV на участок кольца из пула кадра — setSRV( слот, буфер ) привяжет его.
		// Без копии в свой буфер и без барьеров: копия перед каждым инстансным вызовом останавливала бы конвейер
		std::vector<Descriptor>& pool = m_transientDescriptors[m_frameIndex];
		if( m_transientUsed >= pool.size() )
		{
			if( !m_transientExhausted )
				LOG( "Transient descriptor pool of the frame is exhausted (" + std::to_string( pool.size() ) + "): per-frame buffers are not bound" );
			m_transientExhausted = true;
			buffer.setRingView( 0 );
			return;
		}
		const Descriptor& descriptor = pool[m_transientUsed++];
		const bool raw = ( buffer.desc().usage & BufferUsage::raw ) != 0 || buffer.desc().stride == 0;
		const uint32_t stride = raw ? 4 : buffer.desc().stride;
		D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
		viewDesc.Format = raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
		viewDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		viewDesc.Buffer.FirstElement = m_writeOffset / stride;
		viewDesc.Buffer.NumElements = std::max<uint32_t>( m_writeBytes / stride, 1 );
		viewDesc.Buffer.StructureByteStride = raw ? 0 : stride;
		viewDesc.Buffer.Flags = raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
		m_device->CreateShaderResourceView( m_constantRing.handle(), &viewDesc, descriptor.cpu );
		buffer.setRingView( descriptor.index );
		return;
	}
	// Буфер со своим ресурсом: из участка кольца в него копией
	barrier( buffer.handle(), D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, true );
	flushBarriers();
	m_commandList->CopyBufferRegion( buffer.handle(), 0, m_constantRing.handle(), m_writeOffset, m_writeBytes );
	barrier( buffer.handle(), steadyAccess( buffer.desc().usage ) );
}

void DMD3D::setName( const Buffer& buffer, const std::string& name )
{
	if( buffer.handle() )
		buffer.handle()->SetName( toWide( name ).c_str() );
}

void DMD3D::setName( const Texture& texture, const std::string& name )
{
	if( texture.handle() )
		texture.handle()->SetName( toWide( name ).c_str() );
}
