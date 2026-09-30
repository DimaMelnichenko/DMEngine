#include "ScattererPass.h"
#include <algorithm>
#include "D3D\DMD3D.h"
#include "Shaders\lod_transition.h"

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

bool ScatterPass::createInstanceBuffer( InstanceBuffer& instances, uint32_t stride, uint32_t count )
{
	BufferDesc desc;
	desc.size = stride * count;
	desc.stride = stride;
	desc.usage = BufferUsage::shaderResource | BufferUsage::unorderedAccess | BufferUsage::structured;
	if( !DMD3D::instance().createBuffer( desc, nullptr, instances.buffer ) )
		return false;

	// Обычный RWStructuredBuffer: место под инстанс шейдер берёт из счётчика в indirect-аргументах
	// и проверяет ёмкость, append-буфер ёмкость не ограничивал бы
	return DMD3D::instance().createStorageView( instances.buffer, {}, instances.uav );
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
	// Площадь части кольца между радиусами (без π, как ringArea)
	const auto bandArea = [&]( float from, float to )
	{
		from = std::max( from, nearBorder );
		to = std::min( to, farBorder );
		return to > from ? to * to - from * from : 0.0f;
	};
	// Дальность LOD у экземпляра с полосой перехода — в пределах этих долей дальности модели
	const float minScale = LOD_DISTANCE_MIN_SCALE;
	const float maxScale = LOD_DISTANCE_MAX_SCALE;

	m_variants = {};
	float cumulative = 0.0f;
	uint32_t offsets[2] = {};	// обычный буфер и буфер перехода
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
			// Обычный список — полоса LOD, расширенная на разброс; перехода — зоны вокруг дальностей к нему и от него
			const bool hasPrevious = lod > 0;
			const bool hasNext = lod + 1 < lodCount;
			const float bandNear = hasPrevious ? variant.lodEnd[lod - 1] * minScale : 0.0f;
			const float bandFar = hasNext ? variant.lodEnd[lod] * maxScale : farBorder;
			float transitionArea = 0.0f;
			if( hasPrevious )
				transitionArea += bandArea( variant.lodEnd[lod - 1] * minScale, variant.lodEnd[lod - 1] * maxScale );
			if( hasNext )
				transitionArea += bandArea( variant.lodEnd[lod] * minScale, variant.lodEnd[lod] * maxScale );

			const uint32_t sections = std::max( 1u, std::min( variant.sectionCount[lod], maxSections ) );
			m_hasSections = m_hasSections || sections > 1;
			for( uint32_t transition = 0; transition < 2; ++transition )
			{
				const float area = transition ? transitionArea : bandArea( bandNear, bandFar );
				const uint32_t listCapacity = std::max( minListCapacity, static_cast<uint32_t>( capacity * share * area / ringArea ) );
				uint32_t* list = m_variants.lists[listIndex( v, lod, transition != 0 )];
				list[0] = offsets[transition];
				list[1] = listCapacity;
				list[2] = sections;
				offsets[transition] += listCapacity;
			}
		}
	}

	// Параметры слоя пишутся каждый кадр — кольцо констант; варианты меняются редко (setDitheredLodTransition), а
	// читаются каждым проходом — постоянный буфер, обновляемый при смене
	BufferDesc variantsDesc;
	variantsDesc.size = sizeof( VariantsBuffer );
	variantsDesc.usage = BufferUsage::constant;
	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( PopulateParams ), m_populateParamsBuffer ) ||
		!DMD3D::instance().createBuffer( variantsDesc, &m_variants, m_variantsBuffer ) )
		return false;

	if( !createInstanceBuffer( m_instances, sizeof( ScatterItem ), offsets[0] ) ||
		!createInstanceBuffer( m_transitions, sizeof( ScatterTransitionItem ), offsets[1] ) )
		return false;

	// Список — свой участок буфера: вершинный шейдер читает инстансы по SV_InstanceID от начала участка
	for( uint32_t v = 0; v < variantCount; ++v )
	{
		for( uint32_t lod = 0; lod < static_cast<uint32_t>( m_variants.variants[v].y ); ++lod )
		for( uint32_t transition = 0; transition < 2; ++transition )
		{
			const uint32_t list = listIndex( v, lod, transition != 0 );
			BufferViewDesc viewDesc;
			viewDesc.firstElement = m_variants.lists[list][0];
			viewDesc.elementCount = m_variants.lists[list][1];
			if( !DMD3D::instance().createShaderView( transition ? m_transitions.buffer : m_instances.buffer, viewDesc, m_instanceSRVs[list] ) )
				return false;
		}
	}

	// Аргументы DrawIndexedInstancedIndirect — по пять чисел на секцию списка
	BufferDesc desc;
	desc.size = sizeof( m_initialArgs );
	desc.usage = BufferUsage::unorderedAccess | BufferUsage::indirectArgs | BufferUsage::raw;
	// Нули: пока расчёт травы (клавиша 3) не запускался, отрисовка по этим аргументам не рисует ничего.
	// Без начальных данных содержимое буфера не определено, и число инстансов могло оказаться любым
	const uint32_t emptyArgs[maxLists * maxSections * 5] = {};
	if( !DMD3D::instance().createBuffer( desc, emptyArgs, m_argsBuffer ) )
		return false;

	BufferViewDesc uavDesc;
	uavDesc.raw = true;
	return DMD3D::instance().createStorageView( m_argsBuffer, uavDesc, m_argsUAV );
}

void ScatterPass::setDitheredLodTransition( uint32_t variant, bool dithered )
{
	if( variant >= m_populateParams.variantCount )
		return;
	const float value = dithered ? 1.0f : 0.0f;
	if( m_variants.variants[variant].z != value )
	{
		m_variants.variants[variant].z = value;
		m_variantsChanged = true;
	}
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
	DMD3D::instance().updateBuffer( m_argsBuffer, m_initialArgs, sizeof( m_initialArgs ) );
}

PassDesc ScatterPass::passDesc( const char* name ) const
{
	PassDesc pass;
	pass.name = name;
	pass.writes = { { &m_argsUAV, "indirect args" }, { &m_instances.uav, "instances" }, { &m_transitions.uav, "LOD transition instances" } };
	return pass;
}

void ScatterPass::copySectionCounts( DMComputeShader& shader )
{
	if( !m_hasSections )
		return;
	DMD3D::instance().setConstantBuffer( SRVType::cs, 7, m_variantsBuffer );
	shader.setUAVBuffer( 0, m_argsUAV );
	shader.dispatchGroups( 1, 1, 1 );
}

void ScatterPass::populate( DMComputeShader& shader, uint16_t gridDim )
{
	Device::updateResourceData<PopulateParams>( m_populateParamsBuffer, m_populateParams );
	if( m_variantsChanged )
	{
		DMD3D::instance().updateBuffer( m_variantsBuffer, &m_variants, sizeof( m_variants ) );
		m_variantsChanged = false;
	}
	DMD3D::instance().setConstantBuffer( SRVType::cs, 4, m_populateParamsBuffer );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 7, m_variantsBuffer );

	shader.setUAVBuffer( 0, m_argsUAV );
	shader.setUAVBuffer( 1, m_instances.uav );
	shader.setUAVBuffer( 2, m_transitions.uav );

	shader.Dispatch( gridDim, gridDim, 0.0f );
}

const ShaderView& ScatterPass::instances( uint32_t list )
{
	return m_instanceSRVs[list];
}

const Buffer& ScatterPass::args()
{
	return m_argsBuffer;
}

ScatterPass::PopulateParams& ScatterPass::populateParams()
{
	return m_populateParams;
}

}
