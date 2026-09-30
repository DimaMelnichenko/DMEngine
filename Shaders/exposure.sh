////////////////////////////////////////////////////////////////////////////////
// Экспозиция кадра — как Eye Adaptation в UE: состояние живёт на GPU (PostProcess, буфер ExposureState), его пишет
// Shaders/exposure_adapt.cs после замера. Буфер сцены хранит яркость, уже умноженную на экспозицию прошлого кадра
// (pre-exposure): в физических единицах (солнце ~10⁵ лк, диск ~10⁸ кд/м²) иначе переполнился бы R16F (до 65504).
// Шейдеры с освещением домножают результат на preExposure(), постобработка приводит к новой экспозиции —
// exposureRatio(). Материалы без освещения пишут «цвет на экране» как есть: в этом буфере он и так экранный
////////////////////////////////////////////////////////////////////////////////

#ifndef EXPOSURE_SH
#define EXPOSURE_SH

#include "slots.h"
#include "bindless.sh"

// Раскладка — PostProcess::ExposureState
struct ExposureState
{
	float exposure;			// последняя посчитанная экспозиция: с ней рисуется сцена следующего кадра
	float sceneExposure;	// экспозиция, с которой нарисован цвет сцены этого кадра
	float ev100;			// EV100 после адаптации, без поправки экспозиции
	float padding;
};

#ifndef EXPOSURE_STATE_WRITE
DM_SRV( StructuredBuffer<ExposureState>, g_exposureState, SLOT_EXPOSURE );

// Множитель яркости для записи в буфер сцены (проходы сцены: до замера этого кадра)
float preExposure()
{
	return g_exposureState[0].exposure;
}

// Постобработка после замера: цвет сцены × exposureRatio() — яркость с новой экспозицией
float exposureRatio()
{
	return g_exposureState[0].exposure / max( g_exposureState[0].sceneExposure, 1e-30f );
}

// Новая экспозиция: яркость с ней / currentExposure() — снова кд/м²
float currentExposure()
{
	return g_exposureState[0].exposure;
}
#endif

// Экспозиция по EV100 (S. Lagarde, C. de Rousiers, «Moving Frostbite to PBR», 2014): 1 / максимальная яркость без
// пересвета при светочувствительности 100, L_max = 1,2 · 2^EV100
float exposureFromEV100( float ev100 )
{
	return 1.0f / ( 1.2f * exp2( ev100 ) );
}

// EV100 по средней яркости сцены, кд/м²: отражённый свет экспонометра, калибровка K = 12,5
float ev100FromLuminance( float luminance )
{
	return log2( max( luminance, 1e-10f ) * 100.0f / 12.5f );
}

#endif
