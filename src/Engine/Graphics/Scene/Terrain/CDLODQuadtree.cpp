#include "CDLODQuadtree.h"
#include <algorithm>
#include <cmath>
#include "RenderView.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace
{

// Узел ближе диапазона LOD: расстояние от отрезка «глаза — ноги» (origin и на eyeHeight ниже; в полёте — точка) до коробки
// узла не больше radius — как lodDistance в cdlod.vs
bool lodRangeHitsBox( const XMFLOAT3& origin, float eyeHeight, float radius, const XMFLOAT3& boxMin, const XMFLOAT3& boxMax )
{
	float dx = std::max( { boxMin.x - origin.x, 0.0f, origin.x - boxMax.x } );
	float dy = std::max( { boxMin.y - origin.y, 0.0f, origin.y - eyeHeight - boxMax.y } );
	float dz = std::max( { boxMin.z - origin.z, 0.0f, origin.z - boxMax.z } );
	return dx * dx + dy * dy + dz * dz <= radius * radius;
}

}

namespace GS
{

void CDLODQuadtree::initialize( const XMFLOAT2& origin, float leafSize, float extent )
{
	m_origin = origin;
	m_leafSize = leafSize;
	m_extent = extent;

	// Уровней столько, чтобы корень покрыл всю область
	m_levelCount = 1;
	while( m_levelCount < maxLevels && nodeSize( m_levelCount - 1 ) < extent )
		++m_levelCount;

	m_nodesPerSide.resize( m_levelCount );
	for( uint32_t level = 0; level < m_levelCount; ++level )
		m_nodesPerSide[level] = static_cast<uint32_t>( std::ceil( extent / nodeSize( level ) ) );
	m_lod = Lod();
	m_overflowLogged = false;
}

float CDLODQuadtree::nodeSize( uint32_t level ) const
{
	return m_leafSize * static_cast<float>( 1u << level );
}

void CDLODQuadtree::computeLod( const LodSettings& settings, const std::vector<float>& heightExtent )
{
	float minRange = 0.0f;
	for( uint32_t level = 0; level < m_levelCount; ++level )
	{
		const float extent = level < heightExtent.size() ? heightExtent[level] : 0.0f;
		const float size = nodeSize( level );
		const float diagonal = std::sqrt( 2.0f * size * size + extent * extent );
		minRange = std::max( minRange, diagonal / morphStartRatio / static_cast<float>( 1u << level ) );
	}

	float range = std::max( settings.lodDistance, minRange );
	float previousRange = 0.0f;
	for( uint32_t level = 0; level < m_levelCount; ++level )
	{
		const float morphStart = previousRange + ( range - previousRange ) * morphStartRatio;
		m_lod.ranges[level] = range;
		m_lod.morph[level] = XMFLOAT4( morphStart, 1.0f / ( range - morphStart ), 0.0f, 0.0f );

		previousRange = range;
		range *= 2.0f;
	}

	// Уровни мельче листа — от самого мелкого из рисуемых (meshLevels) к −1: диапазон −1 — половина диапазона листа, каждый
	// следующий — вдвое меньше (при «LOD distance» 128 м — 64, 32 … 2 м); самый мелкий полон в finestRadius, его диапазон — не
	// меньше радиуса / morphStartRatio. Без трещин (как выше): граница уровня с более крупным — не дальше его диапазона и
	// диагонали узла (склон до 45° и запас на дисплейсмент), морфинг крупного начинается через morphStartRatio разницы
	// диапазонов — она не меньше диагонали / morphStartRatio. Морфинг листа — дальше 0,66 его диапазона, за диапазоном −1
	m_lod.meshLevels = std::min( settings.meshLevels, detailLevels );
	float detailPrevious = 0.0f;
	float previousDiagonal = 0.0f;
	for( uint32_t depth = detailLevels; depth >= 1; --depth )
	{
		if( depth > m_lod.meshLevels )
		{
			m_lod.detailRanges[depth - 1] = 0.0f;	// не рисуется
			m_lod.detailMorph[depth - 1] = XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
			continue;
		}
		float rangeAt = m_lod.ranges[0] / static_cast<float>( 1u << depth );
		if( depth == m_lod.meshLevels )
			rangeAt = std::max( rangeAt, settings.finestRadius / morphStartRatio );
		rangeAt = std::min( std::max( rangeAt, detailPrevious + previousDiagonal / morphStartRatio ), m_lod.ranges[0] * 0.5f );
		const float morphStart = detailPrevious + ( rangeAt - detailPrevious ) * morphStartRatio;
		m_lod.detailRanges[depth - 1] = rangeAt;
		m_lod.detailMorph[depth - 1] = XMFLOAT4( morphStart, 1.0f / std::max( rangeAt - morphStart, 1e-3f ), 0.0f, 0.0f );
		detailPrevious = rangeAt;
		const float size = nodeSize( 0 ) / static_cast<float>( 1u << depth );
		const float margin = settings.detailMargin;
		previousDiagonal = std::sqrt( 2.0f * size * size + ( size + margin ) * ( size + margin ) );
	}
}

void CDLODQuadtree::select( const RenderView& view, const Bounds& bounds, std::vector<Patch>& patches )
{
	const uint32_t top = m_levelCount - 1;
	for( uint32_t z = 0; z < m_nodesPerSide[top]; ++z )
	{
		for( uint32_t x = 0; x < m_nodesPerSide[top]; ++x )
		{
			if( selectNode( view, bounds, top, x, z, patches ) )
				continue;
			// Корень дальше диапазона своего уровня — рисуется им же: родителя у него нет
			const Box box = bounds.node( top, x, z );
			if( view.frustum.checkBox( box.min, box.max ) )
				for( uint32_t quarter = 0; quarter < 4; ++quarter )
					addPatch( top, x, z, quarter, patches );
		}
	}
}

bool CDLODQuadtree::selectNode( const RenderView& view, const Bounds& bounds, uint32_t level, uint32_t x, uint32_t z,
								std::vector<Patch>& patches )
{
	const Box box = bounds.node( level, x, z );
	const XMFLOAT3& cameraPosition = view.lodOrigin;
	const float eyeHeight = view.lodEyeHeight;

	if( !lodRangeHitsBox( cameraPosition, eyeHeight, m_lod.ranges[level], box.min, box.max ) )
		return false;

	// Невидимый узел считается обработанным: рисовать его не нужно ни ему, ни родителю
	if( !view.frustum.checkBox( box.min, box.max ) )
		return true;

	// Лист у камеры — уровни мельче листа (−1, −2 …) везде: квадродерево однородно, переходы без трещин
	if( level == 0 && lodRangeHitsBox( cameraPosition, eyeHeight, m_lod.detailRanges[0], box.min, box.max ) )
	{
		for( uint32_t quarter = 0; quarter < 4; ++quarter )
			if( !selectDetailNode( view, bounds, 1, x * 2 + ( quarter & 1 ), z * 2 + ( quarter >> 1 ), patches ) )
				addPatch( 0, x, z, quarter, patches );
		return true;
	}
	if( level == 0 || !lodRangeHitsBox( cameraPosition, eyeHeight, m_lod.ranges[level - 1], box.min, box.max ) )
	{
		for( uint32_t quarter = 0; quarter < 4; ++quarter )
			addPatch( level, x, z, quarter, patches );
		return true;
	}

	// Четверти, которые дочерний узел не взял (он дальше своего диапазона), рисуются с детализацией этого уровня
	const uint32_t childCount = m_nodesPerSide[level - 1];
	for( uint32_t quarter = 0; quarter < 4; ++quarter )
	{
		const uint32_t cx = x * 2 + ( quarter & 1 );
		const uint32_t cz = z * 2 + ( quarter >> 1 );
		if( cx >= childCount || cz >= childCount )
			continue;

		if( !selectNode( view, bounds, level - 1, cx, cz, patches ) )
			addPatch( level, x, z, quarter, patches );
	}

	return true;
}

void CDLODQuadtree::addPatch( uint32_t level, uint32_t x, uint32_t z, uint32_t quarter, std::vector<Patch>& patches )
{
	const float size = nodeSize( level );
	const float halfSize = size * 0.5f;
	const XMFLOAT2 corner( x * size + ( quarter & 1 ) * halfSize, z * size + ( quarter >> 1 ) * halfSize );
	if( corner.x >= m_extent || corner.y >= m_extent )
		return;
	push( { XMFLOAT2( m_origin.x + corner.x, m_origin.y + corner.y ), halfSize, static_cast<float>( level ) }, patches );
}

bool CDLODQuadtree::selectDetailNode( const RenderView& view, const Bounds& bounds, uint32_t depth, uint32_t nx, uint32_t nz,
									  std::vector<Patch>& patches )
{
	// Узел — внутри листа сетки области
	if( ( nx >> depth ) >= m_nodesPerSide[0] || ( nz >> depth ) >= m_nodesPerSide[0] )
		return true;
	const Box box = bounds.detailNode( depth, nx, nz );
	const XMFLOAT3& origin = view.lodOrigin;
	if( !lodRangeHitsBox( origin, view.lodEyeHeight, m_lod.detailRanges[depth - 1], box.min, box.max ) )
		return false;
	if( !view.frustum.checkBox( box.min, box.max ) )
		return true;
	if( depth >= m_lod.meshLevels || !lodRangeHitsBox( origin, view.lodEyeHeight, m_lod.detailRanges[depth], box.min, box.max ) )
	{
		for( uint32_t quarter = 0; quarter < 4; ++quarter )
			addDetailPatch( depth, nx, nz, quarter, patches );
		return true;
	}
	for( uint32_t quarter = 0; quarter < 4; ++quarter )
		if( !selectDetailNode( view, bounds, depth + 1, nx * 2 + ( quarter & 1 ), nz * 2 + ( quarter >> 1 ), patches ) )
			addDetailPatch( depth, nx, nz, quarter, patches );
	return true;
}

void CDLODQuadtree::addDetailPatch( uint32_t depth, uint32_t nx, uint32_t nz, uint32_t quarter, std::vector<Patch>& patches )
{
	const float size = nodeSize( 0 ) / static_cast<float>( 1u << depth );
	const float halfSize = size * 0.5f;
	const XMFLOAT2 corner( nx * size + ( quarter & 1 ) * halfSize, nz * size + ( quarter >> 1 ) * halfSize );
	if( corner.x >= m_extent || corner.y >= m_extent )
		return;
	push( { XMFLOAT2( m_origin.x + corner.x, m_origin.y + corner.y ), halfSize, -static_cast<float>( depth ) }, patches );
}

void CDLODQuadtree::push( const Patch& patch, std::vector<Patch>& patches )
{
	if( patches.size() >= maxPatches )
	{
		if( !m_overflowLogged )
			LOG( "CDLOD: more than " + std::to_string( maxPatches ) + " patches in a view, the rest are not drawn" );
		m_overflowLogged = true;
		return;
	}
	patches.push_back( patch );
}

}
