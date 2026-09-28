////////////////////////////////////////////////////////////////////////////////
// Filename: light.vs
// Вершинный шейдер материала PBR. С define DEPTH_ONLY — вариант «только глубина» (Shaders/depth_only.sh): позиция и UV
// без нормалей, для проходов без цвета. Растения с WindWeight > 0 гнутся ветром уровня (Shaders/wind.sh). С define
// LOD_DITHER — экземпляр в полосе смены LOD: отдаёт пиксельному шейдеру долю перехода (Shaders/lod_dither.sh)
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"
#include "instance.sh"
#include "depth_only.sh"
#include "pbr_material.sh"
#include "wind.sh"

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
	// Перед instanceIndex: пиксельный шейдер без дизеринга читает начало выхода
#ifdef LOD_DITHER
	nointerpolation float lodDither : TEXCOORD2;
#endif
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

#ifdef LOD_DITHER
// Доля смены LOD: у расстановки — поле экземпляра в списке перехода, у модели уровня — константа объекта
float lodDither( uint instanceIndex )
{
#if defined(INST_POS)
	return g_instanceData[instanceIndex].lodDither;
#else
	return cb_lodDither;
#endif
}
#endif

// Положение вершины в мире — один путь у полного варианта и «только глубина»
float4 vertexWorldPosition( VertexInputType input )
{
	float4 position = float4( input.position.xyz, 1.0f );

	// instance defines block
	#if defined(INSTANCE_INCLUDE)
		position.xyz = calcInstance( position.xyz, input.instanceIndex );
	#endif

	const float4x4 world = objectWorldMatrix( input.instanceIndex );
	float4 worldPosition = mul( position, world );

	// Ветер: корень растения и высота вершины над ним. У расстановки корень — экземпляр на земле (origin модели внизу),
	// высота — локальная высота × размер; у модели уровня — начало её мировой матрицы
	[branch] if( g_windWeight > 0.0f && cb_windStrength > 0.0f )
	{
	#if defined(INST_POS)
		const InstanceParam instance = g_instanceData[input.instanceIndex];
		const float3 root = mul( float4( instance.position, 1.0f ), world ).xyz;
		#ifdef INST_SCALE
			const float height = input.position.y * instance.scale;
		#else
			const float height = input.position.y;
		#endif
	#else
		const float3 root = world[3].xyz;
		const float height = worldPosition.y - root.y;
	#endif
		worldPosition.xyz += windOffset( root, max( height, 0.0f ), g_windWeight );
	}
	return worldPosition;
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
#ifdef LOD_DITHER
	output.lodDither = lodDither( input.instanceIndex );
#endif
	return output;
}

#else

PixelInputType main(VertexInputType input)
{
    PixelInputType output;

	output.tex = input.tex;

	output.instanceIndex = input.instanceIndex;
#ifdef LOD_DITHER
	output.lodDither = lodDither( input.instanceIndex );
#endif

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
