
#include "slots.h"
#include "bindless.sh"

#ifdef INST_MATRIX
// Экземпляры моделей уровня одним вызовом (Renderer::drawMeshInstanced): мировая матрица и матрица нормалей
// экземпляра вместо cb_worldMatrix, по SV_InstanceID. Раскладка — Renderer::InstanceTransform
struct InstanceTransform
{
	float4x4 world;
	float4x4 worldInverseTranspose;
};

DM_SRV( StructuredBuffer<InstanceTransform>, g_instanceTransforms, SLOT_INSTANCE_DATA );
#endif

#if defined(INST_POS) || defined(INST_SCALE) || defined(INST_TEX) || defined(INST_ROTATE) || defined(INST_COLOR)

#define INSTANCE_INCLUDE

struct InstanceParam
{
#ifdef INST_POS
	float3 position;
#endif
#ifdef INST_SCALE
	float scale;
#endif
#ifdef INST_ROTATE
	float4 rotation;	// кватернион
#endif
#ifdef INST_TEX
	float2 texCoord;
	float2 dummy;
#endif
#ifdef INST_COLOR
	float4 color;
#endif
#ifdef LOD_DITHER
	// Доля смены LOD дизерингом (Shaders/lod_dither.sh) — только в списках перехода расстановки: их буфер —
	// ScatterPass::ScatterTransitionItem
	float  lodDither;
	float3 lodDitherPadding;
#endif
};

DM_SRV( StructuredBuffer<InstanceParam>, g_instanceData, SLOT_INSTANCE_DATA );
// Инстансы расстановки лежат в пуле слоя, а вызов рисует список вида — индексы в пуле (Shaders/scatter.cs): начало
// списка — root-константа команды ExecuteIndirect, SV_InstanceID — номер в списке
DM_SRV( StructuredBuffer<uint>, g_instanceIndices, SLOT_INSTANCE_INDICES );
cbuffer DrawConstants : register( SLOT_CB_DRAW )
{
	uint4 g_drawConstants;	// x — начало списка индексов инстансов вызова
};

float3 rotateByQuaternion( float3 v, float4 q )
{
	return v + 2.0f * cross( q.xyz, cross( q.xyz, v ) + q.w * v );
}

// Направление (нормаль, касательная) поворачивается вместе с инстансом; масштаб одинаков по осям и его не меняет
float3 calcInstanceDirection( float3 direction, uint instanceIndex )
{
#ifdef INST_ROTATE
	return rotateByQuaternion( direction, g_instanceData[instanceIndex].rotation );
#else
	return direction;
#endif
}

float3 calcInstance( float3 originPosition, uint instanceIndex )
{
	InstanceParam instanceItem = g_instanceData[instanceIndex];
	float3 result = originPosition;

#ifdef INST_ROTATE
	result = rotateByQuaternion( result, instanceItem.rotation );
#endif

#ifdef INST_SCALE
	result *= instanceItem.scale;
#endif	

#ifdef INST_POS
	result += instanceItem.position;
#endif

	return result;

}
#endif

// Индекс инстанса в данных инстансов по SV_InstanceID: у расстановки — через список вида, у матриц инстансов — сам номер
uint instanceSlot( uint instanceId )
{
#if defined(INSTANCE_INCLUDE)
	return g_instanceIndices[g_drawConstants.x + instanceId];
#else
	return instanceId;
#endif
}
