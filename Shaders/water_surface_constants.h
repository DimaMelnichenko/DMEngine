////////////////////////////////////////////////////////////////////////////////
// Константы поверхности воды (WaterSurface, Shaders/water_surface.sh — water.vs, water.ps). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_WATER_SURFACE_CONSTANTS_H
#define DM_WATER_SURFACE_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( WaterSurfaceBuffer, SLOT_CB_MATERIAL )
{
	DM_FIELD( uint, size );				// ячеек симуляции по стороне
	DM_FIELD( float, cellSize );			// м
	DM_FIELD( float, worldSize );			// м
	DM_FIELD( uint, tilesPerSide );
	DM_FIELD( float3, absorption );		// поглощение в воде по каналам, 1/м
	DM_FIELD( float, rippleStretch );		// рябь быстрой воды вытянута вдоль течения во столько раз
	DM_FIELD( float3, scatterColor );		// цвет рассеяния в толще воды (доля освещённости неба)
	DM_FIELD( float, rippleScale );		// метров на повтор текстуры ряби
	DM_FIELD( float, rippleStrength );	// наклон нормали ряби на быстрой воде
	DM_FIELD( float, calmRipple );		// и на стоячей (ветер)
	DM_FIELD( float, refraction );		// сдвиг выборки цвета сцены по нормали, доля экрана на метр толщины
	DM_FIELD( float, roughness );			// шероховатость поверхности (размытие отражения неба, ширина блика)
	DM_FIELD( float, foamSpeed );			// пена — быстрее этого, м/с (полная — вдвое быстрее)
	DM_FIELD( float, foamShear );			// и там, где сдвиг скорости больше, 1/с
	DM_FIELD( float, detailDistance );	// м: ближе — поверхность у русел рисует детальная плитка (0 — её нет)
	DM_FIELD( float, foamSlope );			// пена слива — где поверхность детальной воды круче, м на м (полная — втрое круче)
	// Точная земля — урез по ней (water.ps): карта высот (слот 3) в метры и детальная плитка (terrain_detail.sh)
	DM_FIELD( float, terrainHeightMultiplier );
	DM_FIELD( float, terrainHeightOffset );
	DM_FIELD( float, terrainDetailTile );	// сторона детальной плитки, м; 0 — её нет
	// Пена из текстуры (Textures: water_foam, Tools/pack_water_foam.py): метров на повтор поперёк течения (0 — текстуры
	// нет, пена — узор по ряби) и ширина края по маске
	DM_FIELD( float, foamTextureRepeat );
	DM_FIELD( float, foamSoftness );
	DM_FIELD( float3, surfacePadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
