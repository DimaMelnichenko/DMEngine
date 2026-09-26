
#include "slots.h"

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
};

StructuredBuffer<InstanceParam> g_instanceData: register( SLOT_INSTANCE_DATA );

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