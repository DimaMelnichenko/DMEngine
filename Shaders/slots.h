////////////////////////////////////////////////////////////////////////////////
// Слоты ресурсов шейдеров — общие для C++ и HLSL. Ресурсы привязываются bindless: по номеру слота в таблице привязок
// вызова (Shaders/bindless.sh — индексы дескрипторов в общей куче, DMD3D::setSRV / setUAV пишут их по слотам).
// Константные буферы и сэмплеры — регистры root signature: DM_REGISTER в HLSL даёт register( b0 ), в C++ — номер 0.
// Слоты SRV / UAV — просто номера: в HLSL ресурс объявляется DM_SRV( тип, имя, слот ) / DM_UAV( тип, имя, слот ),
// в C++ — DMD3D::setSRV( стадия, слот, вид ) / setUAV( слот, вид ). Слоты, которые использует только один шейдер и
// его объект (текстуры материала, буферы прохода), остаются в своих файлах, но в диапазонах ниже
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_SHADER_SLOTS_H
#define DM_SHADER_SLOTS_H

#ifdef __cplusplus
#define DM_REGISTER( type, index ) index
#else
#define DM_REGISTER( type, index ) type##index
#endif

// Константные буферы — root CBV, у всех стадий
#define SLOT_CB_FRAME		DM_REGISTER( b, 0 )	// камера, время, число источников (common.vs, ConstantBuffers)
#define SLOT_CB_OBJECT		DM_REGISTER( b, 1 )	// матрица объекта
#define SLOT_CB_MATERIAL	DM_REGISTER( b, 2 )	// параметры материала (PBR, террейн, трава)
#define SLOT_CB_PASS		DM_REGISTER( b, 2 )	// параметры прохода без материала (небо, постобработка, compute — DMComputeShader)
#define SLOT_CB_SHADOW		DM_REGISTER( b, 3 )	// каскады теней солнца (shadows.sh, ShadowCascades) — пиксельные шейдеры
// b4…b7 — свои буферы прохода (compute расстановки и частиц); b8 — таблица привязок вызова (bindless.sh)
#define SLOT_CB_BINDINGS	DM_REGISTER( b, 8 )
#define SLOT_CB_COUNT		8						// root CBV b0…b7

// Текстуры и буферы — номера слотов таблицы привязок вызова: t0…t15 — ресурсы материала или прохода, t16 — данные
// объекта, u0…u7 — UAV compute-проходов
#define SLOT_INSTANCE_DATA	16	// вершинный шейдер: данные инстансов (instance.sh, расстановка)
#define SLOT_TRANSIENT_COUNT 17	// слотов SRV вызова: t0…t16
#define SLOT_UAV_COUNT		8	// слотов UAV вызова: u0…u7

// Ресурсы сцены — привязываются один раз за кадр и живут до следующего
#define SLOT_LIGHTS			100	// источники света (lighting.sh, DMLightDriver)
#define SLOT_IBL_IRRADIANCE	101	// освещение окружением: гармоники рассеянного света (ibl.sh, SkyAtmosphere)
#define SLOT_IBL_SPECULAR	102	// префильтрованный cubemap отражений
#define SLOT_IBL_BRDF		103	// таблица BRDF
#define SLOT_SHADOW_MAP		104	// карта теней солнца: массив каскадов (shadows.sh, ShadowCascades)
#define SLOT_EXPOSURE		105	// экспозиция кадра: pre-exposure и новая (exposure.sh, PostProcess)
#define SLOT_AERIAL_PERSPECTIVE	106	// объём воздушной перспективы (aerial_perspective.sh, SkyAtmosphere)
#define SLOT_SCENE_FIRST	100
#define SLOT_SCENE_COUNT	7

// Таблица привязок вызова — root-константы b8, DM_BINDING_COUNT DWORD: индекс дескриптора по слоту
#define DM_BINDING_UAV_BASE		17	// u0…u7 → 17…24
#define DM_BINDING_SCENE_BASE	25	// t100…t106 → 25…31
#define DM_BINDING_COUNT		32

// Сэмплеры — статические в root signature: s0…s7 общие (samplers.sh, DMSamplerState)
#define SLOT_SAMPLER_SHADOW	DM_REGISTER( s, 8 )		// сравнение глубины для карты теней (PCF 2×2)

#endif
