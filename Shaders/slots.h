////////////////////////////////////////////////////////////////////////////////
// Слоты ресурсов шейдеров — общие для C++ и HLSL. В HLSL макрос даёт регистр (register( SLOT_LIGHTS ) → t100),
// в C++ — номер слота (DMD3D::setSRV( SRVType::ps, SLOT_LIGHTS, … ) → 100). Слоты, которые использует только
// один шейдер и его объект (текстуры материала, буферы прохода), остаются в своих файлах, но в диапазонах ниже
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_SHADER_SLOTS_H
#define DM_SHADER_SLOTS_H

#ifdef __cplusplus
#define DM_SLOT( type, index ) index
#else
#define DM_SLOT( type, index ) type##index
#endif

// Константные буферы — у всех стадий
#define SLOT_CB_FRAME		DM_SLOT( b, 0 )	// камера, время, число источников (common.vs, ConstantBuffers)
#define SLOT_CB_OBJECT		DM_SLOT( b, 1 )	// матрица объекта
#define SLOT_CB_MATERIAL	DM_SLOT( b, 2 )	// параметры материала (PBR, террейн, трава)
#define SLOT_CB_PASS		DM_SLOT( b, 2 )	// параметры прохода без материала (небо, постобработка, compute — DMComputeShader)
#define SLOT_CB_SHADOW		DM_SLOT( b, 3 )	// каскады теней солнца (shadows.sh, ShadowCascades) — пиксельные шейдеры
// b4 и дальше — свои буферы прохода (compute расстановки и частиц)

// Текстуры и буферы. t0…t15 — ресурсы материала или прохода, t16… — данные объекта; всё до
// SLOT_TRANSIENT_COUNT отвязывается в конце кадра (DMD3D::EndScene)
#define SLOT_INSTANCE_DATA	DM_SLOT( t, 16 )	// вершинный шейдер: данные инстансов (instance.sh, расстановка)
#define SLOT_TRANSIENT_COUNT 50

// Ресурсы сцены — привязываются один раз за кадр и живут до следующего
#define SLOT_LIGHTS			DM_SLOT( t, 100 )	// источники света (lighting.sh, DMLightDriver)
#define SLOT_IBL_IRRADIANCE	DM_SLOT( t, 101 )	// освещение окружением: гармоники рассеянного света (ibl.sh, SkyAtmosphere)
#define SLOT_IBL_SPECULAR	DM_SLOT( t, 102 )	// префильтрованный cubemap отражений
#define SLOT_IBL_BRDF		DM_SLOT( t, 103 )	// таблица BRDF
#define SLOT_SHADOW_MAP		DM_SLOT( t, 104 )	// карта теней солнца: массив каскадов (shadows.sh, ShadowCascades)
#define SLOT_EXPOSURE		DM_SLOT( t, 105 )	// экспозиция кадра: pre-exposure и новая (exposure.sh, PostProcess)
#define SLOT_AERIAL_PERSPECTIVE	DM_SLOT( t, 106 )	// объём воздушной перспективы (aerial_perspective.sh, SkyAtmosphere)

// Сэмплеры: s0…s7 — общие (samplers.sh, DMSamplerState)
#define SLOT_SAMPLER_SHADOW	DM_SLOT( s, 8 )		// сравнение глубины для карты теней (PCF 2×2)

#endif
