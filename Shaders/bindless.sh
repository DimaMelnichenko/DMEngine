////////////////////////////////////////////////////////////////////////////////
// Bindless-привязка ресурсов (SM 6.6, ResourceDescriptorHeap). Таблица привязок вызова — root-константы b8
// (DM_BINDING_COUNT DWORD, DMD3D ставит их перед вызовом): индексы дескрипторов в общей куче по слотам —
// t0…t16 → 0…16, u0…u7 → DM_BINDING_UAV_BASE…. Ресурсы сцены t100… — константный буфер b10 (DM_SCENE_BINDING_COUNT
// DWORD): DMD3D копирует их индексы в кольцо констант, когда они меняются (как uniform buffer текстур сцены в UE).
// Ресурс объявляется макросом: глобальная static-переменная, инициализированная из ResourceDescriptorHeap при
// входе в шейдер, дальше используется как обычный ресурс. Непривязанный слот — индекс 0: пустой SRV (нули)
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_BINDLESS_SH
#define DM_BINDLESS_SH

#include "slots.h"

cbuffer DrawBindings : register( SLOT_CB_BINDINGS )
{
	uint4 g_bindings[DM_BINDING_COUNT / 4];
};

cbuffer SceneBindings : register( SLOT_CB_SCENE_BINDINGS )
{
	uint4 g_sceneBindings[DM_SCENE_BINDING_COUNT / 4];
};

#define DM_BINDING( i ) ( g_bindings[( i ) / 4][( i ) % 4] )
#define DM_SCENE_BINDING( i ) ( g_sceneBindings[( i ) / 4][( i ) % 4] )
// Слот известен при компиляции: выбор массива — константа; индекс ветки, которая не выбрана, — 0 (без выхода за массив)
#define DM_SRV_INDEX( slot ) ( ( slot ) >= SLOT_SCENE_FIRST ? DM_SCENE_BINDING( ( slot ) >= SLOT_SCENE_FIRST ? ( slot ) - SLOT_SCENE_FIRST : 0 ) \
															  : DM_BINDING( ( slot ) >= SLOT_SCENE_FIRST ? 0 : ( slot ) ) )
#define DM_UAV_INDEX( slot ) DM_BINDING( DM_BINDING_UAV_BASE + ( slot ) )
// Texture2D / Texture2DArray / TextureCube / Texture3D / StructuredBuffer / ByteAddressBuffer (и RW-варианты) по слоту
#define DM_SRV( type, name, slot ) static type name = ResourceDescriptorHeap[DM_SRV_INDEX( slot )]
#define DM_UAV( type, name, slot ) static type name = ResourceDescriptorHeap[DM_UAV_INDEX( slot )]

#endif
