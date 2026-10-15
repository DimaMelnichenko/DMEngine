////////////////////////////////////////////////////////////////////////////////
// Константы импостера (ImpostorMaterial::setParams, Shaders/impostor.sh). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_IMPOSTOR_CONSTANTS_H
#define DM_IMPOSTOR_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( ImpostorBuffer, SLOT_CB_MATERIAL )
{
	DM_FIELD( float4, impostorBounds );		// сфера модели: xyz — центр в её координатах, w — радиус
	DM_FIELD( float4, impostorExtents );		// xyz — половины ящика LOD0 модели (центр — центр сферы): по ним размер карточки
	DM_FIELD( float4, impostorTransmission );	// xyz — цвет пропускания (множитель альбедо), w — множитель доли
	// x — доля прошедшего света, рассеянная вперёд пиком, y — ширина пика (TransmissionForward, TransmissionForwardRoughness
	// секции модели, которая пропускает свет), zw — запас
	DM_FIELD( float4, impostorTransmissionForward );
	DM_FIELD( uint, impostorFrames );		// кадров по стороне сетки
	DM_FIELD( float, impostorAlphaCutoff );
	DM_FIELD( float, impostorRoughness );
	DM_FIELD( float, impostorOcclusion );		// сила затенения окружающего света по запечённому (G глубины кадров)
	// x — затемнение низа: цвет у корня × (1 − x), к верху модели — без затемнения (ImpostorMaterial::setShading);
	// y — высота верха модели над её началом координат (ящик LOD0); z — сила самотени кроны (impostor.ps), w — запас
	DM_FIELD( float4, impostorShade );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
