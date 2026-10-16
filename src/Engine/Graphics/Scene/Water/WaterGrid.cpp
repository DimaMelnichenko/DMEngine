#include "WaterGrid.h"

namespace GS
{

bool WaterGrid::initialize()
{
	return DMD3D::instance().createShaderConstantBuffer( sizeof( WaterGridBuffer ), m_constants );
}

void WaterGrid::prepare( const WaterField& field, const StaticWater& water )
{
	m_field = &field;
	m_water = &water;
	m_height = field.terrain->terrainHeight();
	m_detail = water.detailTileCount() > 0 && water.detailBlockCount() > 0 && m_height.detailIndex;
}

uint32_t WaterGrid::groupCount( bool detailGroups ) const
{
	return detailGroups ? m_water->detailBlockCount() : m_water->flowTileCount();
}

void WaterGrid::beginPass( const char* name, bool detailGroups, std::vector<PassDesc::Read> reads, std::vector<PassDesc::Write> writes )
{
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& groups = detailGroups ? m_water->detailBlocksView() : m_water->flowTilesView();
	PassDesc desc;
	desc.name = name;
	desc.reads = { { &m_field->stateView, "water state" }, { &groups, "grid groups" } };
	if( m_detail )
		desc.reads.insert( desc.reads.end(), { { &m_water->detailWaterView(), "detail water" }, { m_height.detailIndex, "terrain detail index" },
											   { &m_water->detailTileCoordsView(), "detail tile coordinates" } } );
	desc.reads.insert( desc.reads.end(), reads.begin(), reads.end() );
	desc.writes = std::move( writes );
	d3d.beginPass( desc );

	WaterGridBuffer params = {};
	params.gridWorldSize = m_height.worldSize;
	params.gridCellSize = m_field->cellSize;
	params.gridTileSize = m_water->detailTileSize();
	params.gridWetDepth = WaterField::visibleDepth;
	params.gridSide = m_water->detailSide();
	params.gridSize = m_field->size;
	params.gridGroupCount = groupCount( detailGroups );
	params.gridDetail = m_detail ? 1u : 0u;
	Device::updateResourceData( m_constants, params );
	d3d.setConstantBuffer( 6, m_constants );
	if( m_detail )
	{
		d3d.setSRV( 0, m_water->detailWaterView() );
		d3d.setSRV( 1, *m_height.detailIndex );
		d3d.setSRV( 2, m_water->detailTileCoordsView() );
	}
	d3d.setSRV( 3, groups );
	d3d.setSRV( 4, m_field->stateView );
}

}
