#include "ScattererPass.h"
#include <algorithm>
#include <cstring>
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

bool ScatterPass::createPool( PoolBuffer& pool, uint32_t stride, uint32_t count, const char* name )
{
	BufferDesc desc;
	desc.size = stride * count;
	desc.stride = stride;
	desc.usage = BufferUsage::shaderResource | BufferUsage::unorderedAccess | BufferUsage::structured;
	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createBuffer( desc, nullptr, pool.buffer ) )
		return false;
	d3d.setName( pool.buffer, name );
	// Обычный RWStructuredBuffer: место шейдер берёт по счётчику в g_counters и проверяет ёмкость
	return d3d.createStorageView( pool.buffer, {}, pool.uav ) && d3d.createShaderView( pool.buffer, {}, pool.srv );
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
	uint32_t poolCapacity[2] = {};	// пул инстансов и пул перехода — сумма ёмкостей их списков
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
			for( uint32_t transition = 0; transition < 2; ++transition )
			{
				const float area = transition ? transitionArea : bandArea( bandNear, bandFar );
				const uint32_t listCapacity = std::max( minListCapacity, static_cast<uint32_t>( capacity * share * area / ringArea ) );
				uint32_t* list = m_variants.lists[listIndex( v, lod, transition != 0 )];
				list[1] = listCapacity;
				list[2] = sections;
				poolCapacity[transition] += listCapacity;
			}
		}
	}
	// Списки индексов вида — подряд: сначала обычные, затем перехода
	uint32_t indexOffset = 0;
	for( uint32_t transition = 0; transition < 2; ++transition )
	for( uint32_t v = 0; v < variantCount; ++v )
	for( uint32_t lod = 0; lod < static_cast<uint32_t>( m_variants.variants[v].y ); ++lod )
	{
		uint32_t* list = m_variants.lists[listIndex( v, lod, transition != 0 )];
		list[3] = indexOffset;
		indexOffset += list[1];
	}
	m_populateParams.itemCapacity = poolCapacity[0];
	// В пуле перехода экземпляр занимает два места (уходящий и приходящий LOD), в списках — по одному в каждом
	m_populateParams.transitionCapacity = poolCapacity[1];
	m_populateParams.indexStride = indexOffset;

	// Параметры слоя пишутся каждый кадр — кольцо констант; варианты меняются редко (setDitheredLodTransition), а
	// читаются каждым проходом — постоянный буфер, обновляемый при смене
	DMD3D& d3d = DMD3D::instance();
	BufferDesc variantsDesc;
	variantsDesc.size = sizeof( VariantsBuffer );
	variantsDesc.usage = BufferUsage::constant;
	if( !d3d.createShaderConstantBuffer( sizeof( PopulateParams ), m_populateParamsBuffer ) ||
		!d3d.createBuffer( variantsDesc, &m_variants, m_variantsBuffer ) )
		return false;

	if( !createPool( m_items, sizeof( ScatterItem ), poolCapacity[0], "Scatter instances" ) ||
		!createPool( m_transitions, sizeof( ScatterTransitionItem ), poolCapacity[1], "Scatter LOD transition instances" ) ||
		!createPool( m_indices, sizeof( uint32_t ), indexOffset * maxViews, "Scatter instance indices" ) )
		return false;

	// Счётчики (пулы, списки по видам, группы по видам) — байтовый буфер: UAV для расстановки, счётчики команд читает
	// ExecuteIndirect. Команды — по виду и группе подряд
	BufferDesc countersDesc;
	countersDesc.size = countersSize;
	countersDesc.usage = BufferUsage::unorderedAccess | BufferUsage::indirectArgs | BufferUsage::raw;
	BufferDesc commandsDesc;
	commandsDesc.size = maxViews * maxGroups * commandStride;
	commandsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::indirectArgs | BufferUsage::raw;
	BufferViewDesc rawView;
	rawView.raw = true;
	if( !d3d.createBuffer( countersDesc, nullptr, m_counters ) || !d3d.createStorageView( m_counters, rawView, m_countersUAV ) ||
		!d3d.createBuffer( commandsDesc, nullptr, m_commands ) || !d3d.createStorageView( m_commands, rawView, m_commandsUAV ) )
		return false;
	d3d.setName( m_counters, "Scatter counters" );
	d3d.setName( m_commands, "Scatter indirect commands" );

	// Таблицы секций и групп — структурные буферы, обновляются в populate при смене
	BufferDesc sectionsDesc;
	sectionsDesc.size = sizeof( m_sectionArgs );
	sectionsDesc.stride = sizeof( m_sectionArgs[0] );
	sectionsDesc.usage = BufferUsage::shaderResource | BufferUsage::structured;
	BufferDesc groupsDesc;
	groupsDesc.size = sizeof( m_groups );
	groupsDesc.stride = sizeof( m_groups[0] );
	groupsDesc.usage = BufferUsage::shaderResource | BufferUsage::structured;
	if( !d3d.createBuffer( sectionsDesc, m_sectionArgs, m_sectionArgsBuffer ) || !d3d.createShaderView( m_sectionArgsBuffer, {}, m_sectionArgsSRV ) ||
		!d3d.createBuffer( groupsDesc, m_groups, m_groupsBuffer ) || !d3d.createShaderView( m_groupsBuffer, {}, m_groupsSRV ) )
		return false;
	d3d.setName( m_sectionArgsBuffer, "Scatter section args" );
	d3d.setName( m_groupsBuffer, "Scatter groups" );
	m_tablesChanged = true;
	return true;
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

void ScatterPass::setSectionArgs( uint32_t list, uint32_t section, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset, uint32_t group )
{
	if( list >= maxLists || section >= maxSections || group >= maxGroups )
		return;
	uint32_t* args = m_sectionArgs[list * maxSections + section];
	const uint32_t values[4] = { indexCount, indexOffset, vertexOffset, group + 1 };
	if( memcmp( args, values, sizeof( values ) ) != 0 )
	{
		memcpy( args, values, sizeof( values ) );
		m_tablesChanged = true;
	}
}

void ScatterPass::clearSectionArgs( uint32_t list, uint32_t section )
{
	if( list >= maxLists || section >= maxSections )
		return;
	uint32_t* args = m_sectionArgs[list * maxSections + section];
	if( args[3] != 0 )
	{
		memset( args, 0, sizeof( m_sectionArgs[0] ) );
		m_tablesChanged = true;
	}
}

void ScatterPass::rebuildGroups()
{
	// Ёмкость группы — число секций списков в ней (по команде на каждую в виде), команды групп — подряд
	memset( m_groupCapacity, 0, sizeof( m_groupCapacity ) );
	for( const uint32_t* args : m_sectionArgs )
	{
		if( args[3] )
			++m_groupCapacity[args[3] - 1];
	}
	uint32_t base = 0;
	for( uint32_t g = 0; g < maxGroups; ++g )
	{
		m_groupBase[g] = base;
		m_groups[g][0] = base;
		m_groups[g][1] = m_groupCapacity[g];
		m_groups[g][2] = m_groups[g][3] = 0;
		base += m_groupCapacity[g];
	}
}

void ScatterPass::resetCounters()
{
	DMD3D::instance().clearStorageView( m_countersUAV );
}

PassDesc ScatterPass::passDesc( const char* name ) const
{
	PassDesc pass;
	pass.name = name;
	pass.writes = { { &m_countersUAV, "counters" }, { &m_items.uav, "instances" }, { &m_transitions.uav, "LOD transition instances" },
					{ &m_indices.uav, "instance indices" }, { &m_commandsUAV, "indirect commands" } };
	return pass;
}

void ScatterPass::populate( DMComputeShader& shader, uint16_t gridDim )
{
	DMD3D& d3d = DMD3D::instance();
	Device::updateResourceData<PopulateParams>( m_populateParamsBuffer, m_populateParams );
	if( m_variantsChanged )
	{
		d3d.updateBuffer( m_variantsBuffer, &m_variants, sizeof( m_variants ) );
		m_variantsChanged = false;
	}
	if( m_tablesChanged )
	{
		rebuildGroups();
		d3d.updateBuffer( m_sectionArgsBuffer, m_sectionArgs, sizeof( m_sectionArgs ) );
		d3d.updateBuffer( m_groupsBuffer, m_groups, sizeof( m_groups ) );
		m_tablesChanged = false;
	}
	d3d.setConstantBuffer( 4, m_populateParamsBuffer );
	d3d.setConstantBuffer( 7, m_variantsBuffer );

	shader.setUAVBuffer( 0, m_countersUAV );
	shader.setUAVBuffer( 1, m_items.uav );
	shader.setUAVBuffer( 2, m_transitions.uav );
	shader.setUAVBuffer( 3, m_indices.uav );

	shader.Dispatch( gridDim, gridDim, 0.0f );
}

void ScatterPass::buildCommands( DMComputeShader& shader )
{
	// Поток — секция списка вида; групп потоков хватает на все виды (MAX_VIEWS × MAX_LISTS × MAX_SECTIONS / 64)
	DMD3D& d3d = DMD3D::instance();
	d3d.setConstantBuffer( 4, m_populateParamsBuffer );
	d3d.setConstantBuffer( 7, m_variantsBuffer );
	d3d.setSRV( 3, m_sectionArgsSRV );
	d3d.setSRV( 4, m_groupsSRV );
	shader.setUAVBuffer( 0, m_countersUAV );
	shader.setUAVBuffer( 4, m_commandsUAV );
	shader.dispatchGroups( maxViews * maxLists * maxSections / 64, 1, 1 );
}

ScatterPass::PopulateParams& ScatterPass::populateParams()
{
	return m_populateParams;
}

}
