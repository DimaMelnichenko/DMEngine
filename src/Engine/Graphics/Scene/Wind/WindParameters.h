#pragma once

#include <DirectXMath.h>

namespace GS
{

// Ветер кадра (Wind::parameters) — cb_wind* в Shaders/common.vs, изгиб — Shaders/wind.sh; порыв в точке на CPU —
// Wind::gust. Отдельно от констант кадра (ConstantBuffers.h): ветер — часть ядра без устройства (DMCore)
struct WindParameters
{
	DirectX::XMFLOAT2 direction = DirectX::XMFLOAT2( 0.0f, 1.0f );	// куда дует, нормированное (x, z)
	float strength = 0.0f;
	float speed = 0.0f;			// м/с
	float gustMin = 0.0f;
	float gustMax = 0.0f;
	float gustSize = 1.0f;		// м
	float fieldScale = 0.0f;	// поле ветра по рельефу (SLOT_WIND_FIELD): 1 / сторона карты; 0 — поля нет
};

}
