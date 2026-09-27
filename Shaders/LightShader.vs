////////////////////////////////////////////////////////////////////////////////
// Filename: light.vs
// Вершинный шейдер материала PBR. С define DEPTH_ONLY — вариант «только глубина» (Shaders/depth_only.sh): позиция и UV
// без нормалей, для проходов без цвета
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"
#include "instance.sh"
#include "depth_only.sh"

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
    precise float4 position : SV_POSITION;	// как в DepthOnlyVertexOutput: глубина совпадает с вариантом DEPTH_ONLY
    float2 tex : TEXCOORD0;
    float3 normal : NORMAL;
	float3 tangent : TANGENT0;
	float3 binormal : BINORMAL0;
	float3 worldPosition : TEXCOORD1;
	uint instanceIndex : SV_InstanceID;
};

float4x4 objectWorldMatrix( uint instanceIndex )
{
#ifdef INST_MATRIX
	return g_instanceTransforms[instanceIndex].world;
#else
	return cb_worldMatrix;
#endif
}

// Положение вершины в мире — один путь у полного варианта и «только глубина»
float4 vertexWorldPosition( VertexInputType input )
{
	float4 position = float4( input.position.xyz, 1.0f );

	// instance defines block
	#if defined(INSTANCE_INCLUDE)
		position.xyz = calcInstance( position.xyz, input.instanceIndex );
	#endif

	return mul( position, objectWorldMatrix( input.instanceIndex ) );
}


////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
#ifdef DEPTH_ONLY

DepthOnlyVertexOutput main( VertexInputType input )
{
	DepthOnlyVertexOutput output;
	output.position = mul( mul( vertexWorldPosition( input ), cb_viewMatrix ), cb_projectionMatrix );
	output.tex = input.tex;
	return output;
}

#else

PixelInputType main(VertexInputType input)
{
    PixelInputType output;

	output.tex = input.tex;

	output.instanceIndex = input.instanceIndex;

	float4 worldPosition = vertexWorldPosition( input );
	output.worldPosition = worldPosition.xyz;
    output.position = mul( mul( worldPosition, cb_viewMatrix ), cb_projectionMatrix );

	float4x4 worldMatrix = objectWorldMatrix( input.instanceIndex );
	float3x3 normalMatrix = (float3x3)cb_worldInverseTransposeMatrix;
#ifdef INST_MATRIX
	normalMatrix = (float3x3)g_instanceTransforms[input.instanceIndex].worldInverseTranspose;
#endif

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

#endif
