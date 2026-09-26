#include "ScattererPass.h"
#include "D3D\DMD3D.h"

namespace GS
{

ScatterPass::ScatterPass()
{
}


ScatterPass::~ScatterPass()
{
}

bool ScatterPass::createBuffers()
{
	m_populateParams.capacity = static_cast<uint32_t>( m_maxVertexNum );

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( PopulateParams ), m_populateParamsBuffer, nullptr ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( ArgsBuffer ), m_initArgsBuffer, nullptr ) )
		return false;

	D3D11_BUFFER_DESC desc;
	memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = sizeof( ScatterItem ) * m_maxVertexNum;
	desc.StructureByteStride = sizeof( ScatterItem );
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.CPUAccessFlags = 0;
	if( !DMD3D::instance().CreateBuffer( &desc, nullptr, m_populateBuffers.m_vertexBuffer ) )
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
	viewDesc.Format = DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	viewDesc.Buffer.FirstElement = 0;
	viewDesc.Buffer.NumElements = m_maxVertexNum;
	if( !DMD3D::instance().createSRV( m_populateBuffers.m_vertexBuffer, viewDesc, m_populateBuffers.m_srvVertex ) )
	{
		return false;
	}

	// Обычный RWStructuredBuffer: место под инстанс шейдер берёт из счётчика в indirect-аргументах
	// и проверяет ёмкость, append-буфер ёмкость не ограничивал бы
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.Flags = 0;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = m_maxVertexNum;
	if( !DMD3D::instance().createUAV( m_populateBuffers.m_vertexBuffer, uavDesc, m_populateBuffers.m_uavVertex ) )
	{
		return false;
	}

	///////////////***** args

	memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = 4 * 5;
	desc.StructureByteStride = 4;
	desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	// Нули: пока расчёт травы (клавиша 3) не запускался, отрисовка по этим аргументам не рисует ничего.
	// Без начальных данных содержимое буфера не определено, и число инстансов могло оказаться любым
	const uint32_t emptyArgs[5] = {};
	D3D11_SUBRESOURCE_DATA argsData = {};
	argsData.pSysMem = emptyArgs;
	if( !DMD3D::instance().CreateBuffer( &desc, &argsData, m_populateBuffers.m_argsBuffer ) )
		return false;

	viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	viewDesc.BufferEx.FirstElement = 0;
	viewDesc.BufferEx.NumElements = 5;
	viewDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	if( !DMD3D::instance().createSRV( m_populateBuffers.m_argsBuffer, viewDesc, m_populateBuffers.m_srvArgs ) )
	{
		return false;
	}

	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = 5;
	uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	if( !DMD3D::instance().createUAV( m_populateBuffers.m_argsBuffer, uavDesc, m_populateBuffers.m_uavArgs ) )
	{
		return false;
	}

	return true;
}

void ScatterPass::setInstanceParameters( DMComputeShader& shader, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset )
{
	Device::updateResource<ArgsBuffer>( m_initArgsBuffer, [&]( ArgsBuffer& v )
	{
		v.indexCountPerInstance = indexCount;
		v.baseVertexLocation = vertexOffset;
		v.startIndexLocation = indexOffset;
		v.instanceCount = 0;
		v.startInstanceLocation = 0;
	} );

	DMD3D::instance().setConstantBuffer( SRVType::cs, 3, m_initArgsBuffer );

	shader.setUAVBuffer( 0, m_populateBuffers.m_uavArgs.get() );

	shader.Dispatch( 1, 1, 0.0 );
}

void ScatterPass::populate( DMComputeShader& shader, uint16_t gridDim )
{
	Device::updateResourceData<PopulateParams>( m_populateParamsBuffer.get(), m_populateParams );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 4, m_populateParamsBuffer );

	shader.setUAVBuffer( 0, m_populateBuffers.m_uavArgs.get() );
	shader.setUAVBuffer( 1, m_populateBuffers.m_uavVertex.get() );

	shader.Dispatch( gridDim, gridDim, 0.0f );
}

const com_unique_ptr<ID3D11ShaderResourceView>& ScatterPass::structuredBuffer()
{
	return m_populateBuffers.m_srvVertex;
}

ID3D11Buffer* ScatterPass::args()
{
	return m_populateBuffers.m_argsBuffer.get();
}

ScatterPass::PopulateParams& ScatterPass::populateParams()
{
	return m_populateParams;
}

}