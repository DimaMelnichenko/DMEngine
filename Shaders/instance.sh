
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
	float4 rotation;
#endif
#ifdef INST_TEX
	float2 texCoord;
	float2 dummy;
#endif
#ifdef INST_COLOR
	float4 color;
#endif
};

StructuredBuffer<InstanceParam> g_instanceData: register(t16);

float3 calcInstance( float3 originPosition, uint instanceIndex )
{
	InstanceParam instanceItem = g_instanceData[instanceIndex];
	float3 result = originPosition;

#ifdef INST_ROTATE
	float cosY = cos(instanceItem.rotation.y);
	float sinY = sin(instanceItem.rotation.y);
	float3x3 rotMat = {cosY, 0.0, -sinY, 
						0.0, 1.0, 0.0, 
						sinY, 0.0, cosY };
	result = mul(result, rotMat);
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