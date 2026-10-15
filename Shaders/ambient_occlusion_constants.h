////////////////////////////////////////////////////////////////////////////////
// Константы экранного затенения окружением (AmbientOcclusion, Shaders/gtao.cs). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_AMBIENT_OCCLUSION_CONSTANTS_H
#define DM_AMBIENT_OCCLUSION_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( AmbientOcclusionBuffer, b4 )
{
	DM_FIELD( float2, tanHalfFov );		// тангенсы половин угла обзора по X и Y
	DM_FIELD( float2, depthLinearize );	// _33 и _43 проекции: глубина вида z = _43 / ( d − _33 ), d — обратная глубина буфера
	DM_FIELD( uint2, fullSize );			// буфер глубины, пикселей
	DM_FIELD( uint2, halfSize );			// затенение, пикселей
	DM_FIELD( float, radius );			// радиус, м
	DM_FIELD( float, falloffMul );		// спад веса точки к краю радиуса: saturate( расстояние × mul + add )
	DM_FIELD( float, falloffAdd );
	DM_FIELD( float, pixelsPerMeter );	// пикселей полного кадра на метр на глубине 1 м: (высота / 2) / tg(половины угла по Y)
	DM_FIELD( float, finalPower );		// видимость^power: сильнее в щелях, слабее на открытом
	DM_FIELD( float, maxPixelRadius );	// радиус на экране не больше, пикселей полного кадра (кэш и цена у самой камеры)
	DM_FIELD( float, depthTolerance );	// фильтр: доля глубины, при которой сосед ещё та же поверхность
	DM_FIELD( float, padding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
