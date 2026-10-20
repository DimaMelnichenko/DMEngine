////////////////////////////////////////////////////////////////////////////////
// Какую площадь рисует плитка поверхности воды — одно правило у прохода цвета (water.ps) и поверхности для экранных
// отражений (water_reflection.ps): тайлы 1 м — где нет детальных плиток, детальные плитки — свою площадь, кроме окна
// подробного течения у камеры, сетка окна (WATER_NEAR) — окно там, где рисуют детальные плитки
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_CLIP_SH
#define WATER_CLIP_SH

#include "common.vs"
#include "water_surface.sh"
#include "terrain_detail.sh"
#include "water_near.sh"

// Отбрасывает пиксель, который рисует другая плитка
void clipWaterSurface( WaterPixelInput input )
{
	const float tileSize = WATER_TILE * g_cellSize;
#if defined( WATER_NEAR )
	// Сетка окна — только окно и только где рисуют детальные плитки (вершина в пограничном квадрате — с другой стороны границы)
	const int2 detailTile = int2( floor( input.worldPosition.xz / tileSize ) );
	uint tilesX, tilesZ;
	g_terrainDetailIndex.GetDimensions( tilesX, tilesZ );
	if( !nearSurfaceCovers( input.worldPosition.xz ) || any( detailTile < 0 ) || detailTile.x >= (int)tilesX || detailTile.y >= (int)tilesZ ||
		!waterDetailRendered( detailTile, g_terrainDetailIndex.Load( int3( detailTile, 0 ) ), cb_cameraPosition ) )
		discard;
#elif defined( WATER_DETAIL )
	// Детальная плитка рисует только свою площадь: поле в тексель с каждой стороны — у соседней плитки; окно у камеры —
	// сетка окна
	const float2 local = input.worldPosition.xz / tileSize - waterDetailTile( input.tile );
	if( any( local < 0.0f ) || any( local >= 1.0f ) || nearSurfaceCovers( input.worldPosition.xz ) )
		discard;
#else
	// Где поверхность рисует детальная плитка (у русел, вблизи камеры), грубой нет — то же правило, что у отбора тайлов
	if( g_detailDistance > 0.0f )
	{
		const int2 detailTile = int2( floor( input.worldPosition.xz / tileSize ) );
		uint tilesX, tilesZ;
		g_terrainDetailIndex.GetDimensions( tilesX, tilesZ );
		if( all( detailTile >= 0 ) && detailTile.x < (int)tilesX && detailTile.y < (int)tilesZ &&
			waterDetailRendered( detailTile, g_terrainDetailIndex.Load( int3( detailTile, 0 ) ), cb_cameraPosition ) )
			discard;
	}
#endif
}

#endif
