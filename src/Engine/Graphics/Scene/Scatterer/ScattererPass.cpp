#include "ScattererPass.h"
#include <algorithm>
#include "D3D\DMD3D.h"

namespace GS
{

namespace
{

// Меньше списку не даём: у LOD с узкой полосой кольца инстансов мало, но участок нужен
constexpr uint32_t minListCapacity = 256;

}

ScatterPass::ScatterPass()
{
}


ScatterPass::~ScatterPass()
{
}

bool ScatterPass::createBuffers( const std::vector<Variant>& variants )
{
	const uint32_t variantCount = std::max( 1u, std::min( static_cast<uint32_t>( variants.size() ), maxVariants ) );
	m_populateParams.variantCount = variantCount;

	// Доля варианта — по весу; ёмкость списка — по ожидаемому числу его инстансов: доля варианта × площадь полосы LOD
	// в кольце (плотность ячеек по кольцу одинакова)
	float totalWeight = 0.0f;
	for( uint32_t v = 0; v < variantCount && v < variants.size(); ++v )
		totalWeight += std::max( variants[v].weight, 0.0f );
	const float nearBorder = m_populateParams.nearBorder;
	const float farBorder = m_populateParams.farBorder;
	const float ringArea = std::max( farBorder * farBorder - nearBorder * nearBorder, 1e-3f );

	m_variants = {};
	float cumulative = 0.0f;
	uint32_t offset = 0;
	for( uint32_t v = 0; v < variantCount; ++v )
	{
		const Variant variant = v < variants.size() ? variants[v] : Variant();
		const float share = totalWeight > 0.0f ? std::max( variant.weight, 0.0f ) / totalWeight : 1.0f / variantCount;
		cumulative += share;
		const uint32_t lodCount = std::max( 1u, std::min( variant.lodCount, maxLods ) );
		m_variants.variants[v] = XMFLOAT4( v + 1 == variantCount ? 1.0f : cumulative, static_cast<float>( lodCount ), 0.0f, 0.0f );
		m_variants.lodEnd[v] = XMFLOAT4( variant.lodEnd[0], variant.lodEnd[1], variant.lodEnd[2], variant.lodEnd[3] );

		for( uint32_t lod = 0; lod < lodCount; ++lod )
		{
			const float bandNear = std::max( nearBorder, lod == 0 ? 0.0f : variant.lodEnd[lod - 1] );
			const float bandFar = std::min( farBorder, lod + 1 < lodCount ? variant.lodEnd[lod] : farBorder );
			const float area = bandFar > bandNear ? bandFar * bandFar - bandNear * bandNear : 0.0f;
			const uint32_t listCapacity = std::max( minListCapacity, static_cast<uint32_t>( capacity * share * area / ringArea ) );
			uint32_t* list = m_variants.lists[listIndex( v, lod )];
			list[0] = offset;
			list[1] = listCapacity;
			list[2] = std::max( 1u, std::min( variant.sectionCount[lod], maxSections ) );
			m_hasSections = m_hasSections || list[2] > 1;
			offset += listCapacity;
		}
	}
	const uint32_t totalCapacity = offset;

	D3D11_SUBRESOURCE_DATA variantsData = {};
	variantsData.pSysMem = &m_variants;
	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( PopulateParams ), m_populateParamsBuffer, nullptr ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( VariantsBuffer ), m_variantsBuffer, &variantsData ) )
		return false;

	D3D11_BUFFER_DESC desc = {};
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = sizeof( ScatterItem ) * totalCapacity;
	desc.StructureByteStride = sizeof( ScatterItem );
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	if( !DMD3D::instance().CreateBuffer( &desc, nullptr, m_instanceBuffer ) )
		return false;

	// Список — свой участок буфера: вершинный шейдер читает инстансы по SV_InstanceID от начала участка
	for( uint32_t v = 0; v < variantCount; ++v )
	{
		for( uint32_t lod = 0; lod < static_cast<uint32_t>( m_variants.variants[v].y ); ++lod )
		{
			const uint32_t list = listIndex( v, lod );
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
			viewDesc.Format = DXGI_FORMAT_UNKNOWN;
			viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			viewDesc.Buffer.FirstElement = m_variants.lists[list][0];
			viewDesc.Buffer.NumElements = m_variants.lists[list][1];
			if( !DMD3D::instance().createSRV( m_instanceBuffer, viewDesc, m_instanceSRVs[list] ) )
				return false;
		}
	}

	// Обычный RWStructuredBuffer: место под инстанс шейдер берёт из счётчика в indirect-аргументах
	// и проверяет ёмкость, append-буфер ёмкость не ограничивал бы
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = totalCapacity;
	if( !DMD3D::instance().createUAV( m_instanceBuffer, uavDesc, m_instanceUAV ) )
		return false;

	// Аргументы DrawIndexedInstancedIndirect — по пять чисел на секцию списка
	desc = {};
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = sizeof( m_initialArgs );
	desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	// Нули: пока расчёт травы (клавиша 3) не запускался, отрисовка по этим аргументам не рисует ничего.
	// Без начальных данных содержимое буфера не определено, и число инстансов могло оказаться любым
	const uint32_t emptyArgs[maxLists * maxSections * 5] = {};
	D3D11_SUBRESOURCE_DATA argsData = {};
	argsData.pSysMem = emptyArgs;
	if( !DMD3D::instance().CreateBuffer( &desc, &argsData, m_argsBuffer ) )
		return false;

	uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = maxLists * maxSections * 5;
	uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	return DMD3D::instance().createUAV( m_argsBuffer, uavDesc, m_argsUAV );
}

void ScatterPass::setSectionArgs( uint32_t list, uint32_t section, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset )
{
	uint32_t* args = m_initialArgs + argsOffset( list, section ) / sizeof( uint32_t );
	args[0] = indexCount;		// IndexCountPerInstance
	args[1] = 0;				// InstanceCount — считает Shaders\scatter.cs
	args[2] = indexOffset;		// StartIndexLocation
	args[3] = vertexOffset;		// BaseVertexLocation
	args[4] = 0;				// StartInstanceLocation: у списка свой SRV с начала его участка
}

void ScatterPass::resetArgs()
{
	DMD3D::instance().GetDeviceContext()->UpdateSubresource( m_argsBuffer.get(), 0, nullptr, m_initialArgs, 0, 0 );
}

void ScatterPass::copySectionCounts( DMComputeShader& shader )
{
	if( !m_hasSections )
		return;
	DMD3D::instance().setConstantBuffer( SRVType::cs, 7, m_variantsBuffer );
	shader.setUAVBuffer( 0, m_argsUAV.get() );
	shader.dispatchGroups( 1, 1, 1 );
}

void ScatterPass::populate( DMComputeShader& shader, uint16_t gridDim )
{
	Device::updateResourceData<PopulateParams>( m_populateParamsBuffer.get(), m_populateParams );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 4, m_populateParamsBuffer );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 7, m_variantsBuffer );

	shader.setUAVBuffer( 0, m_argsUAV.get() );
	shader.setUAVBuffer( 1, m_instanceUAV.get() );

	shader.Dispatch( gridDim, gridDim, 0.0f );
}

const com_unique_ptr<ID3D11ShaderResourceView>& ScatterPass::instances( uint32_t list )
{
	return m_instanceSRVs[list];
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
