////////////////////////////////////////////////////////////////////////////////
// Плавная смена LOD (Dithered LOD Transition в UE) — общие для C++ и HLSL числа, как slots.h: полосу перехода и
// разброс дальностей считают и шейдер расстановки (Shaders/scatter.cs), и C++ (ёмкость списков ScatterPass, отбор
// каскадов теней в Scatterer, модели уровня в ModelInstances)
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_LOD_TRANSITION_H
#define DM_LOD_TRANSITION_H

// Полоса перехода — доля дальности LOD: экземпляр рисуется обоими LOD с дополняющими друг друга масками дизеринга
// на расстояниях дальность × (1 ± LOD_TRANSITION_WIDTH / 2)
#define LOD_TRANSITION_WIDTH	0.1f

// Своя дальность LOD у экземпляра расстановки — ближе дальности модели на долю: по случайному числу ячейки и по
// низкочастотному шуму мира (ячейка шума — LOD_JITTER_NOISE_SIZE метров), чтобы граница LOD не шла ровной дугой.
// Только ближе: подробный LOD не уходит дальше дальности модели, и его не приходится рисовать в лишние каскады теней
#define LOD_JITTER_RANDOM		0.1f
#define LOD_JITTER_NOISE		0.1f
#define LOD_JITTER_NOISE_SIZE	8.0f

// Пределы дальности LOD экземпляра — доли дальности модели: без полосы перехода (от LOD_JITTER_MIN_SCALE до 1) и
// вместе с ней (от LOD_DISTANCE_MIN_SCALE до LOD_DISTANCE_MAX_SCALE)
#define LOD_JITTER_MIN_SCALE	( 1.0f - LOD_JITTER_RANDOM - LOD_JITTER_NOISE )
#define LOD_DISTANCE_MIN_SCALE	( LOD_JITTER_MIN_SCALE - LOD_TRANSITION_WIDTH * 0.5f )
#define LOD_DISTANCE_MAX_SCALE	( 1.0f + LOD_TRANSITION_WIDTH * 0.5f )

#endif
