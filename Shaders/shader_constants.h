////////////////////////////////////////////////////////////////////////////////
// Буферы констант — одно описание для C++ и HLSL (как slots.h). Поля буфера перечисляются один раз в общем заголовке
// Shaders\<подсистема>_constants.h, его включают и шейдер, и класс, который буфер заполняет: порядок полей у них один,
// новое поле — в одном месте. В HLSL описание — cbuffer с полями-глобалами <приставка><имя> (приставка буфера —
// DM_FIELD_PREFIX: g_, у констант кадра и объекта cb_), в C++ — структура GS::<буфер> с полями <имя>, типы HLSL —
// типами DirectXMath (float4 — XMFLOAT4, matrix — XMMATRIX, uint — uint32_t). Так:
//
//     #define DM_FIELD_PREFIX g_
//     DM_CONSTANTS( ImpostorBuffer, SLOT_CB_MATERIAL )
//     {
//         DM_FIELD( float4, impostorBounds );		// HLSL — g_impostorBounds, C++ — impostorBounds
//         DM_ARRAY( float4, morphConsts, 16 );
//     };
//     DM_CONSTANTS_END
//     #undef DM_FIELD_PREFIX
//
// Раскладка: HLSL кладёт поля в регистры по 16 байт и не разрывает вектор между ними, а C++ — подряд по 4 байта. Чтобы
// она совпадала, поля идут группами по 16 байт (float3 + float, float2 + float2…), элементы массивов — 4-компонентные
// (float4, uint4, matrix). Расхождение покажет Debug-сборка: ShaderCompiler сверяет раскладку каждого буфера
// констант по отражению шейдера (строка «Constant buffer … differs from C++» в log.txt)
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_SHADER_CONSTANTS_H
#define DM_SHADER_CONSTANTS_H

#define DM_PASTE_( a, b ) a##b
#define DM_PASTE( a, b ) DM_PASTE_( a, b )

#ifdef __cplusplus

#include <cstdint>
#include <DirectXMath.h>

#define DM_TYPE_float float
#define DM_TYPE_float2 DirectX::XMFLOAT2
#define DM_TYPE_float3 DirectX::XMFLOAT3
#define DM_TYPE_float4 DirectX::XMFLOAT4
#define DM_TYPE_matrix DirectX::XMMATRIX
#define DM_TYPE_float4x4 DirectX::XMMATRIX
#define DM_TYPE_int int32_t
#define DM_TYPE_int2 DirectX::XMINT2
#define DM_TYPE_int3 DirectX::XMINT3
#define DM_TYPE_int4 DirectX::XMINT4
#define DM_TYPE_uint uint32_t
#define DM_TYPE_uint2 DirectX::XMUINT2
#define DM_TYPE_uint3 DirectX::XMUINT3
#define DM_TYPE_uint4 DirectX::XMUINT4

#define DM_CONSTANTS( name, slot ) namespace GS { struct alignas( 16 ) name
#define DM_CONSTANTS_END }
#define DM_FIELD( type, name ) DM_TYPE_##type name
#define DM_ARRAY( type, name, count ) DM_TYPE_##type name[count]

#else

#define DM_CONSTANTS( name, slot ) cbuffer name : register( slot )
#define DM_CONSTANTS_END
#define DM_FIELD( type, name ) type DM_PASTE( DM_FIELD_PREFIX, name )
#define DM_ARRAY( type, name, count ) type DM_PASTE( DM_FIELD_PREFIX, name )[count]

#endif

#endif
