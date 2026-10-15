#include "TerrainPaintTool.h"
#include "TerrainPipeline.h"
#include <algorithm>
#include "System.h"

namespace GS
{

void TerrainPaintTool::initialize( TerrainPipeline& pipeline )
{
	m_pipeline = &pipeline;
	// Кисть слоёв и масок (View → «Terrain paint», вьюпорт): с уровнем не сохраняется
	m_properties.setName( "Paint" );
	m_properties.setTransient( true );
	std::vector<std::string> names;
	for( const std::string& target : pipeline.paintTargets() )
	{
		const int32_t layer = TerrainPaint::layerOf( target );
		names.push_back( layer >= 0 ? target + ": " + pipeline.material().layerName( layer ) : target );
	}
	m_properties.insert( "Target", static_cast<int32_t>( 0 ) )->setItems( names )
		->setTooltip( "What the brush paints: a ground layer or a mask (grass, flowers, forest by kind, shrubs)" );
	m_properties.insert( "Mode", static_cast<int32_t>( 0 ) )->setItems( { "Value", "Step" } )
		->setTooltip( "Value - a stroke takes the result to Value and no further; Step - every stroke adds Value (Shift - subtracts). "
					  "Ctrl - erase the paint back to the rules" );
	Property* prop = m_properties.insert( "Value", 1.0f );
	prop->setLow( 0.0f );
	prop->setHigh( 1.0f );
	prop = m_properties.insert( "Radius", 10.0f );
	prop->setLow( 1.0f );
	prop->setHigh( 200.0f );
	prop->setLogarithmic()->setUnit( "m" );
	prop = m_properties.insert( "Falloff", 0.5f );
	prop->setLow( 0.0f );
	prop->setHigh( 1.0f );
	prop->setTooltip( "Soft edge, share of the radius" );
	m_properties.insert( "Show target", true )->setTooltip( "In the Terrain paint mode - the brush target over the ground: orange by "
														   "its value (layer weight, wetness, mask), dark lines every 0.25" );
}

TerrainPaintTool::Brush TerrainPaintTool::brush( bool shift, bool ctrl ) const
{
	Brush brush;
	const PropertyContainer& p = m_properties;
	if( !m_pipeline || !p.exists( "Target" ) )
		return brush;
	const std::vector<std::string> targets = m_pipeline->paintTargets();
	const int32_t target = p["Target"].data<int32_t>();
	if( target >= 0 && static_cast<size_t>( target ) < targets.size() )
		brush.target = targets[target];
	const bool step = p["Mode"].data<int32_t>() == 1;
	brush.mode = ctrl ? TerrainPaint::Mode::erase : !step ? TerrainPaint::Mode::value :
				 shift ? TerrainPaint::Mode::unstep : TerrainPaint::Mode::step;
	brush.value = p["Value"].data<float>();
	brush.radius = std::max( p["Radius"].data<float>(), 0.5f );
	brush.falloff = p["Falloff"].data<float>();
	return brush;
}

int32_t TerrainPaintTool::previewTarget( const ShaderView*& mask ) const
{
	// Цель кисти поверх земли: слой, мокрость или маска (её текстура хранилища — t7 пиксельного шейдера рельефа)
	mask = nullptr;
	if( !m_preview || !m_properties.exists( "Show target" ) || !m_properties["Show target"].data<bool>() )
		return -1;
	const std::string target = brush( false, false ).target;
	const int32_t layer = TerrainPaint::layerOf( target );
	if( layer >= 0 && static_cast<uint32_t>( layer ) < TerrainMaterial::maxLayers )
		return layer;
	if( target == TerrainPaint::wetnessTarget )
		return static_cast<int32_t>( TerrainMaterial::maxLayers );
	if( !target.empty() && System::textures().exists( target ) )
	{
		mask = &System::textures().get( System::textures().id( target ) )->srv();
		return static_cast<int32_t>( TerrainMaterial::maxLayers ) + 1;
	}
	return -1;
}

bool TerrainPaintTool::beginStroke( const std::string& target, TerrainPaint::Mode mode, float value, std::string& reason )
{
	if( !m_pipeline )
	{
		reason = "terrain paint: no terrain";
		return false;
	}
	// Плитки цели до мазка — для отмены
	TerrainPaint::TileMap before = m_pipeline->paint().targetTiles( target );
	if( !m_pipeline->beginPaintStroke( target, mode, value, reason ) )
		return false;
	m_target = target;
	m_before = std::move( before );
	return true;
}

void TerrainPaintTool::dab( float x, float z, float radius, float falloff )
{
	if( m_pipeline )
		m_pipeline->paintDab( x, z, radius, falloff );
}

TerrainPaintTool::Edit TerrainPaintTool::endStroke()
{
	Edit edit;
	if( !painting() )
		return edit;
	const TerrainPaint::Rect touched = m_pipeline->endPaintStroke();
	edit.target = m_target;
	edit.before = std::move( m_before );
	edit.after = m_pipeline->paint().targetTiles( m_target );
	edit.changed = !touched.empty();
	m_before.clear();
	m_target.clear();
	return edit;
}

bool TerrainPaintTool::painting() const
{
	return m_pipeline && m_pipeline->painting();
}

void TerrainPaintTool::setTiles( const std::string& target, const TerrainPaint::TileMap& tiles )
{
	if( m_pipeline )
		m_pipeline->setPaintTiles( target, tiles );
}

bool TerrainPaintTool::stroke( const std::string& target, TerrainPaint::Mode mode, float value, float x, float z, float radius,
							   float falloff, std::string& reason )
{
	if( !beginStroke( target, mode, value, reason ) )
		return false;
	dab( x, z, radius, falloff );
	if( !endStroke().changed )
	{
		reason = "terrain paint: the brush is outside the map";
		return false;
	}
	return true;
}

}
