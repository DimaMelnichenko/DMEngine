////////////////////////////////////////////////////////////////////////////////
// Bindless-привязка ресурсов (SM 6.6, ResourceDescriptorHeap). Таблица привязок вызова — root-константы b8
// (DM_BINDING_COUNT DWORD, DMD3D ставит их перед вызовом): индексы дескрипторов в общей куче по слотам —
// t0…t16 → 0…16, u0…u7 → DM_BINDING_UAV_BASE…, t100…t107 (ресурсы сцены) → DM_BINDING_SCENE_BASE….
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

#define DM_BINDING( i ) ( g_bindings[( i ) / 4][( i ) % 4] )
#define DM_SRV_INDEX( slot ) DM_BINDING( ( slot ) >= SLOT_SCENE_FIRST ? DM_BINDING_SCENE_BASE + ( slot ) - SLOT_SCENE_FIRST : ( slot ) )
#define DM_UAV_INDEX( slot ) DM_BINDING( DM_BINDING_UAV_BASE + ( slot ) )
// Texture2D / Texture2DArray / TextureCube / Texture3D / StructuredBuffer / ByteAddressBuffer (и RW-варианты) по слоту
#define DM_SRV( type, name, slot ) static type name = ResourceDescriptorHeap[DM_SRV_INDEX( slot )]
#define DM_UAV( type, name, slot ) static type name = ResourceDescriptorHeap[DM_UAV_INDEX( slot )]

#endif
