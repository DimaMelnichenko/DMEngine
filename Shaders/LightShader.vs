////////////////////////////////////////////////////////////////////////////////
// Filename: light.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"
#include "instance.sh"

//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float4 position : POSITION0;
    float2 tex : TEXCOORD0;
    float3 normal : NORMAL0;
	float3 tangent : TANGENT0;
	float3 binormal : BINORMAL0;
	uint instanceIndex: SV_InstanceID;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
    float3 normal : NORMAL;
	float3 tangent : TANGENT0;
	float3 binormal : BINORMAL0;
	float3 worldPosition : TEXCOORD1;
	uint instanceIndex : SV_InstanceID;
};


////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;
	float3 worldPosition;

    output.position.xyz = input.position;
	output.position.w = 1.0f;
	
	output.tex = input.tex;
	
	output.instanceIndex = input.instanceIndex;
	
	// instance defines block
	#if defined(INSTANCE_INCLUDE)
		output.position.xyz = calcInstance( output.position.xyz, input.instanceIndex );
	#endif	
    
    

	float4x4 worldMatrix = cb_worldMatrix;
	float3x3 normalMatrix = (float3x3)cb_worldInverseTransposeMatrix;
#ifdef INST_MATRIX
	worldMatrix = g_instanceTransforms[input.instanceIndex].world;
	normalMatrix = (float3x3)g_instanceTransforms[input.instanceIndex].worldInverseTranspose;
#endif

    output.position = mul(output.position, worldMatrix);
	output.worldPosition = output.position.xyz;
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);
    
    
	
	float3 normal = input.normal;
	float3 tangent = input.tangent;
	float3 binormal = input.binormal;
	#if defined(INSTANCE_INCLUDE)
		normal = calcInstanceDirection( normal, input.instanceIndex );
		tangent = calcInstanceDirection( tangent, input.instanceIndex );
		binormal = calcInstanceDirection( binormal, input.instanceIndex );
	#endif

	// Касательная и бинормаль лежат в поверхности и преобразуются как точки, нормаль — обратной транспонированной
	output.normal = normalize( mul( normal, normalMatrix ) );
	output.tangent = normalize( mul( tangent, (float3x3)worldMatrix ) );
	output.binormal = normalize( mul( binormal, (float3x3)worldMatrix ) );
	

    return output;
}