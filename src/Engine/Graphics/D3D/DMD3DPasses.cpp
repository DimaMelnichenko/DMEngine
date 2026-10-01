// DMD3D: барьеры (enhanced barriers) по состояниям подресурсов и проходы — объявление PassDesc, цели, область
// вывода, очистки
#include "DMD3D.h"
#include "Logger\Logger.h"
#include <algorithm>
#include <string>

D3D12_BARRIER_SYNC DMD3D::syncFor( D3D12_BARRIER_ACCESS access )
{
	switch( access )
	{
		case D3D12_BARRIER_ACCESS_COPY_SOURCE:
		case D3D12_BARRIER_ACCESS_COPY_DEST: return D3D12_BARRIER_SYNC_COPY;
		case D3D12_BARRIER_ACCESS_RENDER_TARGET: return D3D12_BARRIER_SYNC_RENDER_TARGET;
		case D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE:
		case D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ: return D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		case D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT: return D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
		case D3D12_BARRIER_ACCESS_INDEX_BUFFER: return D3D12_BARRIER_SYNC_INDEX_INPUT;
		case D3D12_BARRIER_ACCESS_VERTEX_BUFFER: return D3D12_BARRIER_SYNC_VERTEX_SHADING;
		case D3D12_BARRIER_ACCESS_NO_ACCESS: return D3D12_BARRIER_SYNC_NONE;
		default: return D3D12_BARRIER_SYNC_ALL_SHADING;	// SRV, UAV, константы
	}
}

D3D12_BARRIER_ACCESS DMD3D::steadyAccess( uint32_t usage )
{
	// Чем буфер читается после записи с CPU: аргументы — ExecuteIndirect, вершины и индексы — IA, остальное — шейдеры
	if( usage & BufferUsage::indirectArgs )
		return D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
	if( usage & BufferUsage::vertex )
		return D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
	if( usage & BufferUsage::index )
		return D3D12_BARRIER_ACCESS_INDEX_BUFFER;
	if( usage & BufferUsage::constant )
		return D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
	if( usage & BufferUsage::shaderResource )
		return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
	if( usage & BufferUsage::unorderedAccess )
		return D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
	return D3D12_BARRIER_ACCESS_COPY_SOURCE;
}

void DMD3D::barrier( ID3D12Resource* resource, D3D12_BARRIER_ACCESS access, D3D12_BARRIER_LAYOUT layout, const SubresourceRange* range, bool force,
					 D3D12_BARRIER_SYNC sync )
{
	if( !resource )
		return;
	auto found = m_states.find( resource );
	if( found == m_states.end() )
		return;	// задний буфер и ресурсы без учёта состояния — свои барьеры
	ResourceState& state = found->second;
	const D3D12_BARRIER_SYNC syncAfter = sync != D3D12_BARRIER_SYNC_NONE ? sync : syncFor( access );
	const bool uavWrite = access == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
	// Нужен ли барьер подресурсу: другой доступ или layout; запись через UAV после записи через UAV — тоже (порядок)
	const auto needs = [&]( const SubresourceState& sub )
	{
		return force || sub.access != access || ( state.texture && sub.layout != layout ) || ( uavWrite && sub.uavWritten );
	};
	const auto apply = [&]( SubresourceState& sub )
	{
		sub.layout = state.texture ? layout : D3D12_BARRIER_LAYOUT_UNDEFINED;
		sub.sync = syncAfter;
		sub.access = access;
		sub.uavWritten = false;
	};

	if( !state.texture )
	{
		SubresourceState& sub = state.subresources[0];
		if( !needs( sub ) )
			return;
		D3D12_BUFFER_BARRIER bufferBarrier = {};
		bufferBarrier.SyncBefore = sub.sync;
		bufferBarrier.SyncAfter = syncAfter;
		bufferBarrier.AccessBefore = sub.access;
		bufferBarrier.AccessAfter = access;
		bufferBarrier.pResource = resource;
		bufferBarrier.Size = UINT64_MAX;
		m_pendingBufferBarriers.push_back( bufferBarrier );
		apply( sub );
		return;
	}

	// Текстура: диапазон подресурсов вида (мипы куба, срезы каскадов) — одним барьером, если они в одном состоянии,
	// иначе по подресурсу
	uint32_t firstMip = 0, mipCount = state.mipCount, firstSlice = 0, sliceCount = state.arraySize;
	if( range )
	{
		firstMip = std::min( range->firstMip, state.mipCount - 1 );
		mipCount = range->mipCount ? std::min( range->mipCount, state.mipCount - firstMip ) : state.mipCount - firstMip;
		firstSlice = std::min( range->firstSlice, state.arraySize - 1 );
		sliceCount = range->sliceCount ? std::min( range->sliceCount, state.arraySize - firstSlice ) : state.arraySize - firstSlice;
	}
	const auto subresource = [&]( uint32_t mip, uint32_t slice ) -> SubresourceState& { return state.subresources[slice * state.mipCount + mip]; };
	const SubresourceState first = subresource( firstMip, firstSlice );
	bool uniform = true;
	for( uint32_t slice = firstSlice; slice < firstSlice + sliceCount && uniform; ++slice )
		for( uint32_t mip = firstMip; mip < firstMip + mipCount && uniform; ++mip )
		{
			const SubresourceState& sub = subresource( mip, slice );
			uniform = sub.access == first.access && sub.layout == first.layout && sub.sync == first.sync && sub.uavWritten == first.uavWritten;
		}

	const auto push = [&]( const SubresourceState& before, uint32_t mip, uint32_t mips, uint32_t slice, uint32_t slices )
	{
		D3D12_TEXTURE_BARRIER textureBarrier = {};
		textureBarrier.SyncBefore = before.sync;
		textureBarrier.SyncAfter = syncAfter;
		textureBarrier.AccessBefore = before.access;
		textureBarrier.AccessAfter = access;
		textureBarrier.LayoutBefore = before.layout;
		textureBarrier.LayoutAfter = layout;
		textureBarrier.pResource = resource;
		if( mip == 0 && mips == state.mipCount && slice == 0 && slices == state.arraySize )
			textureBarrier.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFFu;
		else
		{
			textureBarrier.Subresources.IndexOrFirstMipLevel = mip;
			textureBarrier.Subresources.NumMipLevels = mips;
			textureBarrier.Subresources.FirstArraySlice = slice;
			textureBarrier.Subresources.NumArraySlices = slices;
			textureBarrier.Subresources.NumPlanes = 1;
		}
		m_pendingTextureBarriers.push_back( textureBarrier );
	};
	if( uniform )
	{
		if( !needs( first ) )
			return;
		push( first, firstMip, mipCount, firstSlice, sliceCount );
		for( uint32_t slice = firstSlice; slice < firstSlice + sliceCount; ++slice )
			for( uint32_t mip = firstMip; mip < firstMip + mipCount; ++mip )
				apply( subresource( mip, slice ) );
		return;
	}
	for( uint32_t slice = firstSlice; slice < firstSlice + sliceCount; ++slice )
		for( uint32_t mip = firstMip; mip < firstMip + mipCount; ++mip )
		{
			SubresourceState& sub = subresource( mip, slice );
			if( !needs( sub ) )
				continue;
			push( sub, mip, 1, slice, 1 );
			apply( sub );
		}
}

void DMD3D::flushBarriers()
{
	if( m_pendingBufferBarriers.empty() && m_pendingTextureBarriers.empty() )
		return;
	D3D12_BARRIER_GROUP groups[2];
	uint32_t count = 0;
	if( !m_pendingBufferBarriers.empty() )
	{
		groups[count].Type = D3D12_BARRIER_TYPE_BUFFER;
		groups[count].NumBarriers = static_cast<UINT32>( m_pendingBufferBarriers.size() );
		groups[count].pBufferBarriers = m_pendingBufferBarriers.data();
		++count;
	}
	if( !m_pendingTextureBarriers.empty() )
	{
		groups[count].Type = D3D12_BARRIER_TYPE_TEXTURE;
		groups[count].NumBarriers = static_cast<UINT32>( m_pendingTextureBarriers.size() );
		groups[count].pTextureBarriers = m_pendingTextureBarriers.data();
		++count;
	}
	m_commandList->Barrier( count, groups );
	m_frameBarriers += static_cast<uint32_t>( m_pendingBufferBarriers.size() + m_pendingTextureBarriers.size() );
	m_pendingBufferBarriers.clear();
	m_pendingTextureBarriers.clear();
}

void DMD3D::beginPass( const PassDesc& pass )
{
	// Барьеры по объявлению: цели — в layout записи, чтение — в состояние чтения, UAV — в запись (по подресурсам вида)
	D3D12_CPU_DESCRIPTOR_HANDLE targets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	m_passFormats = {};
	for( const PassDesc::Target& target : pass.colors )
	{
		if( target.view && target.view->valid() && m_passFormats.colorCount < TargetFormats::maxColors )
		{
			barrier( target.view->resource(), D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET, &target.view->range() );
			targets[m_passFormats.colorCount] = target.view->handle();
			m_passFormats.color[m_passFormats.colorCount++] = target.view->format();
		}
	}
	const bool hasDepth = pass.depth.view && pass.depth.view->valid();
	const D3D12_CPU_DESCRIPTOR_HANDLE depth = hasDepth ? pass.depth.view->handle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
	m_passFormats.depth = hasDepth ? pass.depth.view->format() : DXGI_FORMAT_UNKNOWN;
	if( hasDepth )
		barrier( pass.depth.view->resource(), D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, &pass.depth.view->range() );
	for( const PassDesc::Read& read : pass.reads )
	{
		if( read.view && read.view->valid() )
			barrier( read.view->resource(), D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, &read.view->range() );
	}
	for( const PassDesc::Write& write : pass.writes )
	{
		if( write.view && write.view->valid() )
			barrier( write.view->resource(), D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, &write.view->range() );
	}
	flushBarriers();
	m_commandList->OMSetRenderTargets( m_passFormats.colorCount, m_passFormats.colorCount ? targets : nullptr, FALSE, hasDepth ? &depth : nullptr );

	if( pass.width && pass.height )
	{
		D3D12_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>( pass.width );
		viewport.Height = static_cast<float>( pass.height );
		viewport.MaxDepth = 1.0f;
		const D3D12_RECT scissor = { 0, 0, static_cast<LONG>( pass.width ), static_cast<LONG>( pass.height ) };
		m_commandList->RSSetViewports( 1, &viewport );
		m_commandList->RSSetScissorRects( 1, &scissor );
	}

	// Слоты вызова таблицы привязок — чистые (дескриптор 0 — пустой SRV); слоты сцены живут до следующего кадра
	for( uint32_t i = 0; i < DM_BINDING_SCENE_BASE; ++i )
		m_bindings[i] = 0;
	m_bindingsDirtyGraphics = m_bindingsDirtyCompute = true;

	if( m_recordingPasses )
	{
		finishPassRecord();
		std::string record = std::string( pass.name ) + ":";
		for( const PassDesc::Target& target : pass.colors )
			record += std::string( " color[" ) + target.name + "]";
		if( pass.depth.view )
			record += std::string( " depth[" ) + pass.depth.name + "]";
		if( pass.width && pass.height )
			record += " " + std::to_string( pass.width ) + "x" + std::to_string( pass.height );
		if( pass.colors.empty() && !pass.depth.view )
			record += " compute";
		for( const PassDesc::Read& read : pass.reads )
			record += std::string( " reads[" ) + read.name + "]";
		for( const PassDesc::Write& write : pass.writes )
			record += std::string( " writes[" ) + write.name + "]";
		m_passRecords.push_back( record );
		m_passBarriersStart = m_frameBarriers;
	}
}

void DMD3D::finishPassRecord()
{
	// Барьеры прохода — те, что поставлены с его начала до начала следующего (или до конца кадра)
	if( !m_passRecords.empty() && m_frameBarriers > m_passBarriersStart )
		m_passRecords.back() += " barriers " + std::to_string( m_frameBarriers - m_passBarriersStart );
}

void DMD3D::clearDepth( const TargetView& target, float depth )
{
	if( !target.valid() || !target.isDepth() )
		return;
	barrier( target.resource(), D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, &target.range() );
	flushBarriers();
	m_commandList->ClearDepthStencilView( target.handle(), D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr );
}

void DMD3D::clearTarget( const TargetView& target, const float color[4] )
{
	if( !target.valid() || target.isDepth() )
		return;
	barrier( target.resource(), D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET, &target.range() );
	flushBarriers();
	m_commandList->ClearRenderTargetView( target.handle(), color, 0, nullptr );
}
