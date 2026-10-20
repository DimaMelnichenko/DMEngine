////////////////////////////////////////////////////////////////////////////////
// Константы подробного течения у камеры (NearWaterFlow, Shaders/water_flow.cs с FLOW_NEAR): окно мелкой сетки вокруг
// камеры, вложенное в течение детальной сетки 0,25 м. Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_WATER_NEAR_FLOW_CONSTANTS_H
#define DM_WATER_NEAR_FLOW_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

// Камней русла в окне (NearWaterFlow::maxStones)
#define WATER_NEAR_MAX_STONES 512
// Текселей по стороне квадрата верха камня (StoneTop::cells)
#define WATER_NEAR_STONE_CELLS 16

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( WaterNearFlowBuffer, b7 )
{
	DM_FIELD( int2, nearOrigin );			// первая ячейка окна (сетка мира с шагом nearStep от угла мира, y — по Z)
	DM_FIELD( int2, nearPreviousOrigin );	// окна прошлого кадра: ячейки вне него — новые, начинаются с течения 0,25 м
	DM_FIELD( uint, nearSize );			// ячеек по стороне окна (кратно 8)
	DM_FIELD( float, nearStep );			// сторона ячейки, м
	DM_FIELD( uint, nearStoneCount );		// камней в g_nearStones
	// Камень выше воды — препятствие: проводимость грани и глубина для трения сквозь него — эта доля глубины без камня
	// (вода обходит камень, а ряд камней без просвета не запружает ручей намертво — сочится)
	DM_FIELD( float, nearPassage );
	DM_FIELD( float, nearEddies );			// удержание завихрённости (vorticity confinement), доля
	DM_FIELD( float, nearNudgeShare );		// доля шага: средняя блока 8 × 8 — к течению 0,25 м
	DM_FIELD( uint, nearReset );			// 1 — все ячейки окна — новые (первый кадр, телепорт, правка окна)
	DM_FIELD( float, nearSurface );			// подъём поверхности по давлению — × это (0 — поверхность ровная)
};
DM_CONSTANTS_END

// Узор пены окна по его течению (water_near_foam.cs): слои смещений координат обновляются раз в период по фазе в точке
// (nearPatternPhase, water_near.sh)
DM_CONSTANTS( WaterNearPatternBuffer, b6 )
{
	DM_FIELD( float, nearPatternTime );		// время игры этого кадра, с
	DM_FIELD( float, nearPatternStep );		// с прошлого кадра, с (0 — время стоит: слои те же)
	DM_FIELD( float, nearPatternPeriod );	// период обновления слоя, с
	DM_FIELD( float, nearPatternPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
