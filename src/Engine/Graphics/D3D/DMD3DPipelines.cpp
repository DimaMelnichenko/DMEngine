// DMD3D: состояния, пайплайны (PSO из PipelineDesc, compute-пайплайны) и их кэш на диске — ID3D12PipelineLibrary
#include "DMD3D.h"
#include "Logger\Logger.h"
#include <d3dx12.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

void DMD3D::setState( RasterState state )
{
	m_renderState.raster = state;
}

void DMD3D::setState( DepthState state )
{
	m_renderState.depth = state;
}

void DMD3D::setState( BlendState state )
{
	m_renderState.blend = state;
}

void DMD3D::setState( const RenderState& state )
{
	m_renderState = state;
}

const Pipeline& DMD3D::findOrCreatePipeline( const PipelineDesc& desc, bool warming )
{
	const uint64_t key = desc.key();
	auto found = m_pipelines.find( key );
	if( found != m_pipelines.end() )
		return found->second;

	const uint32_t id = static_cast<uint32_t>( m_pipelines.size() );
	if( m_pipelinesWarm && !warming )
	{
		// Не из списка прогрева: сборка PSO в кадре — фриз; дополнить прогрев (ShaderProgram::warmPipelines)
		++m_lazyPipelines;
		LOG( "Pipeline " + std::to_string( id ) + " is created lazily: raster " + std::to_string( static_cast<int>( desc.state.raster ) ) +
			 ", depth " + std::to_string( static_cast<int>( desc.state.depth ) ) + ", blend " + std::to_string( static_cast<int>( desc.state.blend ) ) +
			 ", topology " + std::to_string( static_cast<int>( desc.topology ) ) + ", colors " + std::to_string( desc.formats.colorCount ) +
			 " (" + std::to_string( desc.formats.colorCount ? desc.formats.color[0] : 0 ) + "), depth format " + std::to_string( desc.formats.depth ) );
	}
	Pipeline& pipeline = m_pipelines.emplace( key, Pipeline( desc, id ) ).first->second;
	createPipelineObject( pipeline );
	return pipeline;
}

void DMD3D::setPipeline( const Pipeline& pipeline )
{
	m_renderState = pipeline.desc().state;
	ID3D12PipelineState* object = pipeline.object();
	m_graphicsPipelineValid = object != nullptr;
	if( object )
	{
		m_commandList->SetPipelineState( object );
		m_commandList->IASetPrimitiveTopology( pipeline.desc().topology );
	}
}

void DMD3D::setComputeShader( const ShaderStage& stage )
{
	if( !stage.valid() )
	{
		m_computePipelineValid = false;
		return;
	}
	ID3D12PipelineState* object = computePipeline( stage );
	m_computePipelineValid = object != nullptr;
	if( object )
		m_commandList->SetPipelineState( object );
}

TargetFormats DMD3D::backBufferFormats()
{
	return TargetFormats::colorTarget( backBufferViewFormat );
}

std::wstring DMD3D::pipelineName( const PipelineDesc& desc ) const
{
	uint64_t hash = m_rootSignatureHash ? m_rootSignatureHash : 14695981039346656037ull;
	const auto mix = [&hash]( uint64_t value )
	{
		for( int i = 0; i < 8; ++i )
		{
			hash ^= ( value >> ( i * 8 ) ) & 0xFF;
			hash *= 1099511628211ull;
		}
	};
	const auto mixText = [&hash]( const char* text )
	{
		for( ; text && *text; ++text )
		{
			hash ^= static_cast<uint8_t>( *text );
			hash *= 1099511628211ull;
		}
	};
	mix( desc.vertex ? desc.vertex->hash() : 0 );
	mix( desc.pixel ? desc.pixel->hash() : 0 );
	mix( desc.geometry ? desc.geometry->hash() : 0 );
	if( desc.layout )
	{
		for( const VertexElement& element : desc.layout->elements() )
		{
			mixText( element.semantic );
			mix( static_cast<uint64_t>( element.semanticIndex ) | static_cast<uint64_t>( element.format ) << 8 |
				 static_cast<uint64_t>( element.slot ) << 24 | static_cast<uint64_t>( element.offset ) << 32 | ( element.perInstance ? 1ull << 63 : 0 ) );
		}
	}
	mix( static_cast<uint64_t>( desc.state.raster ) | static_cast<uint64_t>( desc.state.depth ) << 8 | static_cast<uint64_t>( desc.state.blend ) << 16 |
		 static_cast<uint64_t>( desc.topology ) << 24 );
	mix( PipelineDesc::depthBiasBits( desc.state.depthBias ) );
	for( uint32_t i = 0; i < desc.formats.colorCount; ++i )
		mix( static_cast<uint64_t>( desc.formats.color[i] ) | static_cast<uint64_t>( i ) << 32 );
	mix( static_cast<uint64_t>( desc.formats.depth ) | static_cast<uint64_t>( desc.formats.colorCount ) << 32 );
	wchar_t name[24];
	swprintf_s( name, L"p%016llx", static_cast<unsigned long long>( hash ) );
	return name;
}

bool DMD3D::createPipelineObject( Pipeline& pipeline )
{
	const PipelineDesc& desc = pipeline.desc();
	pipeline.setObject( nullptr );
	if( !desc.vertex || !desc.vertex->valid() )
	{
		LOG( "Pipeline " + std::to_string( pipeline.id() ) + ": no vertex shader" );
		return false;
	}

	// Растеризатор по RasterState (как состояния D3D11 до переезда)
	CD3DX12_RASTERIZER_DESC rasterizer( D3D12_DEFAULT );
	switch( desc.state.raster )
	{
		case RasterState::solid: rasterizer.CullMode = D3D12_CULL_MODE_BACK; break;
		case RasterState::frontCulling: rasterizer.CullMode = D3D12_CULL_MODE_FRONT; break;
		case RasterState::noCulling: rasterizer.CullMode = D3D12_CULL_MODE_NONE; break;
		case RasterState::wireframe:
			rasterizer.FillMode = D3D12_FILL_MODE_WIREFRAME;
			rasterizer.CullMode = D3D12_CULL_MODE_NONE;
			break;
		// Зеркальные меши: лицевые грани — против часовой стрелки
		case RasterState::solidMirrored:
			rasterizer.CullMode = D3D12_CULL_MODE_BACK;
			rasterizer.FrontCounterClockwise = TRUE;
			break;
		case RasterState::noCullingMirrored:
			rasterizer.CullMode = D3D12_CULL_MODE_NONE;
			rasterizer.FrontCounterClockwise = TRUE;
			break;
		// Глубина каскадов теней: без отсечения граней (рельеф, тонкие панели и лепестки тоже отбрасывают тень), без
		// отсечения по глубине (pancaking); смещение глубины — из состояния
		case RasterState::csmShadowDepth:
			rasterizer.CullMode = D3D12_CULL_MODE_NONE;
			rasterizer.DepthClipEnable = FALSE;
			break;
	}
	// Смещение глубины (RenderState::depthBias, у теней — Shadow Slope Bias солнца) «от света»: глубина обратная — знак минус
	rasterizer.SlopeScaledDepthBias = -desc.state.depthBias.slopeScaled;
	rasterizer.DepthBiasClamp = -desc.state.depthBias.clamp;

	// Глубина обратная: «ближе» — GREATER. Без цели глубины проверка выключена, что бы ни просило состояние
	CD3DX12_DEPTH_STENCIL_DESC depth( D3D12_DEFAULT );
	depth.StencilEnable = FALSE;
	depth.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
	switch( desc.state.depth )
	{
		case DepthState::enabled: depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; break;
		case DepthState::readOnly: depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; break;
		case DepthState::readOnlyNearOrEqual:
			depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			break;
		case DepthState::readOnlyEqual:
			depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
			break;
		case DepthState::disabled: depth.DepthEnable = FALSE; break;
	}
	if( desc.formats.depth == DXGI_FORMAT_UNKNOWN )
		depth.DepthEnable = FALSE;

	CD3DX12_BLEND_DESC blend( D3D12_DEFAULT );
	D3D12_RENDER_TARGET_BLEND_DESC& target = blend.RenderTarget[0];
	switch( desc.state.blend )
	{
		case BlendState::alpha:
			target.BlendEnable = TRUE;
			target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			target.SrcBlendAlpha = D3D12_BLEND_ONE;
			target.DestBlendAlpha = D3D12_BLEND_ZERO;
			break;
		case BlendState::additive:
			target.BlendEnable = TRUE;
			target.SrcBlend = D3D12_BLEND_ONE;
			target.DestBlend = D3D12_BLEND_ONE;
			target.SrcBlendAlpha = D3D12_BLEND_ONE;
			target.DestBlendAlpha = D3D12_BLEND_ONE;
			break;
		case BlendState::opaque: break;
	}

	std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
	if( desc.layout )
	{
		for( const VertexElement& element : desc.layout->elements() )
		{
			D3D12_INPUT_ELEMENT_DESC input = {};
			input.SemanticName = element.semantic;
			input.SemanticIndex = element.semanticIndex;
			input.Format = element.format;
			input.InputSlot = element.slot;
			input.AlignedByteOffset = element.offset == VertexElement::appendOffset ? D3D12_APPEND_ALIGNED_ELEMENT : element.offset;
			input.InputSlotClass = element.perInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			input.InstanceDataStepRate = element.perInstance ? 1 : 0;
			elements.push_back( input );
		}
	}

	D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	if( desc.topology == D3D_PRIMITIVE_TOPOLOGY_POINTLIST )
		topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	else if( desc.topology >= D3D_PRIMITIVE_TOPOLOGY_LINELIST && desc.topology <= D3D_PRIMITIVE_TOPOLOGY_LINESTRIP ||
			 desc.topology == D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ || desc.topology == D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ )
		topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	else if( desc.topology >= D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST )
		topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

	D3D12_RT_FORMAT_ARRAY renderTargets = {};
	renderTargets.NumRenderTargets = desc.formats.colorCount;
	for( uint32_t i = 0; i < desc.formats.colorCount; ++i )
		renderTargets.RTFormats[i] = desc.formats.color[i];

	struct Stream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_VS vs;
		CD3DX12_PIPELINE_STATE_STREAM_PS ps;
		CD3DX12_PIPELINE_STATE_STREAM_GS gs;
		CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blend;
		CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencil;
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY topology;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS renderTargets;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT depthFormat;
		CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC sampleDesc;
		CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK sampleMask;
	} stream;
	stream.rootSignature = m_rootSignature.get();
	stream.vs = D3D12_SHADER_BYTECODE{ desc.vertex->data(), desc.vertex->size() };
	if( desc.pixel && desc.pixel->valid() )
		stream.ps = D3D12_SHADER_BYTECODE{ desc.pixel->data(), desc.pixel->size() };
	if( desc.geometry && desc.geometry->valid() )
		stream.gs = D3D12_SHADER_BYTECODE{ desc.geometry->data(), desc.geometry->size() };
	stream.blend = blend;
	stream.rasterizer = rasterizer;
	stream.depthStencil = depth;
	stream.inputLayout = D3D12_INPUT_LAYOUT_DESC{ elements.empty() ? nullptr : elements.data(), static_cast<UINT>( elements.size() ) };
	stream.topology = topologyType;
	stream.renderTargets = renderTargets;
	stream.depthFormat = desc.formats.depth;
	stream.sampleDesc = DXGI_SAMPLE_DESC{ 1, 0 };
	stream.sampleMask = UINT_MAX;
	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = { sizeof( stream ), &stream };

	// Из библиотеки на диске по имени-хэшу; нет — собрать и положить
	const std::wstring name = pipelineName( desc );
	ID3D12PipelineState* object = nullptr;
	HRESULT hr = E_FAIL;
	if( m_pipelineLibrary )
		hr = m_pipelineLibrary->LoadPipeline( name.c_str(), &streamDesc, __uuidof( ID3D12PipelineState ), reinterpret_cast<void**>( &object ) );
	if( FAILED( hr ) )
	{
		hr = m_device->CreatePipelineState( &streamDesc, __uuidof( ID3D12PipelineState ), reinterpret_cast<void**>( &object ) );
		if( FAILED( hr ) )
		{
			LOG( "CreatePipelineState failed for pipeline " + std::to_string( pipeline.id() ) + ": raster " + std::to_string( static_cast<int>( desc.state.raster ) ) +
				 ", depth " + std::to_string( static_cast<int>( desc.state.depth ) ) + ", blend " + std::to_string( static_cast<int>( desc.state.blend ) ) +
				 ", colors " + std::to_string( desc.formats.colorCount ) + ", depth format " + std::to_string( desc.formats.depth ) +
				 ", HRESULT " + std::to_string( static_cast<long>( hr ) ) );
			return false;
		}
		if( m_pipelineLibrary && SUCCEEDED( m_pipelineLibrary->StorePipeline( name.c_str(), object ) ) )
			m_pipelineLibraryDirty = true;
	}
	object->SetName( name.c_str() );
	pipeline.setObject( object );
	return true;
}

ID3D12PipelineState* DMD3D::computePipeline( const ShaderStage& stage )
{
	auto found = m_computePipelines.find( stage.hash() );
	if( found != m_computePipelines.end() )
		return found->second.get();

	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_rootSignature.get();
	desc.CS = D3D12_SHADER_BYTECODE{ stage.data(), stage.size() };
	wchar_t name[24];
	swprintf_s( name, L"c%016llx", static_cast<unsigned long long>( stage.hash() ^ m_rootSignatureHash ) );
	ID3D12PipelineState* object = nullptr;
	HRESULT hr = E_FAIL;
	if( m_pipelineLibrary )
		hr = m_pipelineLibrary->LoadComputePipeline( name, &desc, __uuidof( ID3D12PipelineState ), reinterpret_cast<void**>( &object ) );
	if( FAILED( hr ) )
	{
		hr = m_device->CreateComputePipelineState( &desc, __uuidof( ID3D12PipelineState ), reinterpret_cast<void**>( &object ) );
		if( FAILED( hr ) )
		{
			LOG( "CreateComputePipelineState failed, HRESULT " + std::to_string( static_cast<long>( hr ) ) );
			m_computePipelines.emplace( stage.hash(), nullptr );
			return nullptr;
		}
		if( m_pipelineLibrary && SUCCEEDED( m_pipelineLibrary->StorePipeline( name, object ) ) )
			m_pipelineLibraryDirty = true;
	}
	object->SetName( name );
	if( m_pipelinesWarm )
	{
		++m_lazyPipelines;
		LOG( "Compute pipeline is created lazily (DMComputeShader::Initialize warms it)" );
	}
	return m_computePipelines.emplace( stage.hash(), make_com_ptr<ID3D12PipelineState>( object ) ).first->second.get();
}

bool DMD3D::loadPipelineLibrary()
{
	// Кэш PSO на диске — ID3D12PipelineLibrary: сборка пайплайнов при загрузке уровня из него почти бесплатна. От другого
	// драйвера или адаптера рантайм её не принимает — тогда библиотека пустая и пересобирается
	m_pipelineLibraryData.clear();
	std::ifstream file( "cache/pipelines.bin", std::ios::binary );
	if( file )
		m_pipelineLibraryData.assign( std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() );
	ID3D12PipelineLibrary1* library = nullptr;
	HRESULT hr = m_device->CreatePipelineLibrary( m_pipelineLibraryData.data(), m_pipelineLibraryData.size(), __uuidof( ID3D12PipelineLibrary1 ),
												  reinterpret_cast<void**>( &library ) );
	if( FAILED( hr ) && !m_pipelineLibraryData.empty() )
	{
		LOG( "Pipeline library cache/pipelines.bin is not accepted (driver or adapter changed), rebuilding" );
		m_pipelineLibraryData.clear();
		hr = m_device->CreatePipelineLibrary( nullptr, 0, __uuidof( ID3D12PipelineLibrary1 ), reinterpret_cast<void**>( &library ) );
	}
	if( FAILED( hr ) )
	{
		LOG( "CreatePipelineLibrary failed: pipelines are built without the disk cache" );
		return false;
	}
	m_pipelineLibrary = make_com_ptr<ID3D12PipelineLibrary1>( library );
	m_pipelineLibraryDirty = false;
	if( !m_pipelineLibraryData.empty() )
		LOG( "Pipeline library: " + std::to_string( m_pipelineLibraryData.size() / 1024 ) + " KB from cache/pipelines.bin" );
	return true;
}

void DMD3D::savePipelineLibrary()
{
	if( !m_pipelineLibrary || !m_pipelineLibraryDirty )
		return;
	const SIZE_T size = m_pipelineLibrary->GetSerializedSize();
	std::vector<uint8_t> data( size );
	if( size == 0 || FAILED( m_pipelineLibrary->Serialize( data.data(), size ) ) )
		return;
	std::error_code error;
	std::filesystem::create_directories( "cache", error );
	std::ofstream file( "cache/pipelines.bin", std::ios::binary );
	if( file )
	{
		file.write( reinterpret_cast<const char*>( data.data() ), static_cast<std::streamsize>( size ) );
		LOG( "Pipeline library saved: " + std::to_string( size / 1024 ) + " KB, " + std::to_string( m_pipelines.size() ) + " graphics and " +
			 std::to_string( m_computePipelines.size() ) + " compute pipelines" );
	}
	m_pipelineLibraryDirty = false;
}
