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

bool ScatterPass::createBuffers( uint32_t lodCount )
{
	lodCount = std::max( 1u, std::min( lodCount, maxLods ) );
	m_populateParams.lodCount = lodCount;
	m_populateParams.lodCapacity = capacity / lodCount;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( PopulateParams ), m_populateParamsBuffer, nullptr ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( ArgsBuffer ), m_initArgsBuffer, nullptr ) )
		return false;

	D3D11_BUFFER_DESC desc = {};
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = sizeof( ScatterItem ) * capacity;
	desc.StructureByteStride = sizeof( ScatterItem );
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	if( !DMD3D::instance().CreateBuffer( &desc, nullptr, m_instanceBuffer ) )
		return false;

	// Список LOD — свой участок буфера: вершинный шейдер читает инстансы по SV_InstanceID от начала участка
	for( uint32_t lod = 0; lod < lodCount; ++lod )
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
		viewDesc.Format = DXGI_FORMAT_UNKNOWN;
		viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		viewDesc.Buffer.FirstElement = lod * m_populateParams.lodCapacity;
		viewDesc.Buffer.NumElements = m_populateParams.lodCapacity;
		if( !DMD3D::instance().createSRV( m_instanceBuffer, viewDesc, m_instanceSRVs[lod] ) )
			return false;
	}

	// Обычный RWStructuredBuffer: место под инстанс шейдер берёт из счётчика в indirect-аргументах
	// и проверяет ёмкость, append-буфер ёмкость не ограничивал бы
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = capacity;
	if( !DMD3D::instance().createUAV( m_instanceBuffer, uavDesc, m_instanceUAV ) )
		return false;

	// Аргументы DrawIndexedInstancedIndirect — по пять чисел на LOD
	desc = {};
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = argsOffset( lodCount );
	desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	// Нули: пока расчёт травы (клавиша 3) не запускался, отрисовка по этим аргументам не рисует ничего.
	// Без начальных данных содержимое буфера не определено, и число инстансов могло оказаться любым
	const uint32_t emptyArgs[5 * maxLods] = {};
	D3D11_SUBRESOURCE_DATA argsData = {};
	argsData.pSysMem = emptyArgs;
	if( !DMD3D::instance().CreateBuffer( &desc, &argsData, m_argsBuffer ) )
		return false;

	uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = 5 * lodCount;
	uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	return DMD3D::instance().createUAV( m_argsBuffer, uavDesc, m_argsUAV );
}

void ScatterPass::resetArgs( DMComputeShader& shader, uint32_t lod, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset )
{
	Device::updateResource<ArgsBuffer>( m_initArgsBuffer, [&]( ArgsBuffer& v )
	{
		v.indexCountPerInstance = indexCount;
		v.baseVertexLocation = vertexOffset;
		v.startIndexLocation = indexOffset;
		v.instanceCount = 0;
		v.startInstanceLocation = 0;
		v.argsOffset = argsOffset( lod );
	} );

	DMD3D::instance().setConstantBuffer( SRVType::cs, 3, m_initArgsBuffer );

	shader.setUAVBuffer( 0, m_argsUAV.get() );

	shader.Dispatch( 1, 1, 0.0 );
}

void ScatterPass::populate( DMComputeShader& shader, uint16_t gridDim )
{
	Device::updateResourceData<PopulateParams>( m_populateParamsBuffer.get(), m_populateParams );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 4, m_populateParamsBuffer );

	shader.setUAVBuffer( 0, m_argsUAV.get() );
	shader.setUAVBuffer( 1, m_instanceUAV.get() );

	shader.Dispatch( gridDim, gridDim, 0.0f );
}

const com_unique_ptr<ID3D11ShaderResourceView>& ScatterPass::instances( uint32_t lod )
{
	return m_instanceSRVs[lod];
}

ID3D11Buffer* ScatterPass::args()
{
	return m_argsBuffer.get();
}

ScatterPass::PopulateParams& ScatterPass::populateParams()
{
	return m_populateParams;
}

}
