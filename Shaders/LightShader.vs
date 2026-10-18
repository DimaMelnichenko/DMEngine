////////////////////////////////////////////////////////////////////////////////
// Filename: light.vs
// Вершинный шейдер материала PBR. С define DEPTH_ONLY — вариант «только глубина» (Shaders/depth_only.sh): позиция и UV
// без нормалей, для проходов без цвета. Растения с WindWeight > 0 гнутся ветром уровня (Shaders/wind.sh) и расступаются
// по следам в траве (Shaders/foliage_interaction.sh). С define
// LOD_DITHER — экземпляр в полосе смены LOD: отдаёт пиксельному шейдеру долю перехода (Shaders/lod_dither.sh). С define
// WIND_TREE (материал PBRTree) — ветер дерева по данным второго потока вершин (VertexData::Wind), а не изгиб травы. С define
// SKINNED — модель со скелетом: вершина в позе кадра по костям третьего потока (Shaders/skinning.sh) до всего остального
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"
#include "instance.sh"
#include "depth_only.sh"
#include "pbr_material_constants.h"
#include "wind.sh"
#include "foliage_interaction.sh"
#include "skinning.sh"

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
#ifdef WIND_TREE
	// Второй поток VertexPool (слот 1): начала ветвей первого и второго уровня (xyz, координаты меша) и их веса,
	// доля высоты дерева и вес ряби — как входы Games wind SpeedTree
	float4 windBranch1 : BRANCH0;
	float4 windBranch2 : BRANCH1;
	float2 windWeights : WINDWEIGHTS0;
#endif
#ifdef SKINNED
	// Третий поток VertexPool (слот 2): до 4 костей скелета и их веса
	uint4 joints : JOINTS0;
	float4 weights : WEIGHTS0;
#endif
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
	// Цвет земли у растения расстановки (scatter_ground.sh): rgb — цвет, a — доля в вершине; brightness — множитель своего
	// цвета. У остальных — 0 и 1
	float4 ground : TEXCOORD3;
	float groundBrightness : TEXCOORD4;
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

// Точка в координатах меша — в мир: у экземпляра расстановки — его поворот, размер и положение, затем мировая матрица
float3 meshToWorld( float3 position, uint instanceIndex, float4x4 world )
{
#if defined(INSTANCE_INCLUDE)
	position = calcInstance( position, instanceIndex );
#endif
	return mul( float4( position, 1.0f ), world ).xyz;
}

// Положение вершины в мире — один путь у полного варианта и «только глубина»; frame — базис вершины в мире: ветер гнёт его
// вместе с вершиной (на положение он не влияет, у «только глубина» не нужен)
float4 vertexWorldPosition( VertexInputType input, inout WindFrame frame )
{
	float4 position = float4( input.position.xyz, 1.0f );
	// Номер инстанса вызова → индекс в данных инстансов (у расстановки — через список вида, instance.sh)
	const uint instanceIndex = instanceSlot( input.instanceIndex );

	// instance defines block
	#if defined(INSTANCE_INCLUDE)
		position.xyz = calcInstance( position.xyz, instanceIndex );
	#endif

	const float4x4 world = objectWorldMatrix( instanceIndex );
	float4 worldPosition = mul( position, world );

	// Ветер: корень растения и высота вершины над ним. У расстановки корень — экземпляр на земле (origin модели внизу),
	// высота — локальная высота × размер; у модели уровня — начало её мировой матрицы
#ifdef WIND_TREE
	// Дерево: слои Games wind SpeedTree (Shaders/wind.sh). Корень и начала ветвей — точки меша в мире: у экземпляра
	// расстановки (лес) — через его поворот, размер и положение, у модели уровня — мировой матрицей
	[branch] if( g_windWeight > 0.0f && cb_windStrength > 0.0f )
	{
		float3 normal = input.normal;
		#if defined(INSTANCE_INCLUDE)
			normal = calcInstanceDirection( normal, instanceIndex );
		#endif
		worldPosition.xyz = treeWindPosition( worldPosition.xyz, meshToWorld( float3( 0.0f, 0.0f, 0.0f ), instanceIndex, world ),
											  meshToWorld( input.windBranch1.xyz, instanceIndex, world ), input.windBranch1.w,
											  meshToWorld( input.windBranch2.xyz, instanceIndex, world ), input.windBranch2.w,
											  input.windWeights, normalize( mul( normal, (float3x3)world ) ), g_windWeight, frame );
	}
#else
	// Растение (не дерево): ветер и следы в траве (foliage_interaction.sh) — следы есть и без ветра (-nowind)
	[branch] if( g_windWeight > 0.0f )
	{
	#if defined(INST_POS)
		const InstanceParam instance = g_instanceData[instanceIndex];
		const float3 root = mul( float4( instance.position, 1.0f ), world ).xyz;
		#ifdef INST_SCALE
			const float scale = instance.scale;
		#else
			const float scale = 1.0f;
		#endif
			const float height = max( input.position.y * scale, 0.0f );
		// Высота растёт вдоль оси Y экземпляра (наклон по земле, случайный поворот)
		const float3 up = normalize( mul( calcInstanceDirection( float3( 0.0f, 1.0f, 0.0f ), instanceIndex ), (float3x3)world ) );
		// Своё случайное число растения — по его повороту, а не по месту: кольцевой слой стоит на земле с дисплейсментом
		// (terrainSurfacePoint), и корень сдвигается вместе с камерой — хеш места прыгал бы, и трава дрожала бы на ходу
		#ifdef INST_ROTATE
			const float phase = windPhase( instance.rotation.xyz );
		#else
			const float phase = windPhase( float3( instance.position.x, 0.0f, instance.position.z ) );
		#endif
	#else
		const float3 root = world[3].xyz;
		const float height = max( worldPosition.y - root.y, 0.0f );
		const float3 up = float3( 0.0f, 1.0f, 0.0f );
		const float phase = windPhase( root );
		const float scale = 1.0f;
	#endif
		// След в корне: придавленное растение ветер качает меньше, затем оно ложится по следу дугой от корня (вместе с изгибом
		// ветра); отклик и длина изгиба — параметры материала, длина — в метрах модели (× размер экземпляра)
		const float2 push = foliagePushAt( root );
		const float pressed = length( push );
		[branch] if( cb_windStrength > 0.0f )
		{
			const float2 bend = windBendVector( root, g_windWeight, phase ) * ( 1.0f - 0.8f * pressed * saturate( g_trampleWeight ) );
			worldPosition.xyz += windOffset( bend, height );
			windBendFrame( bend, height, up, frame );
		}
		[branch] if( pressed > 0.01f )
			worldPosition.xyz = foliageTrample( worldPosition.xyz, root, up, push, phase, g_trampleWeight, g_trampleBendLength * scale, frame );
	}
#endif
	return worldPosition;
}


////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
#ifdef DEPTH_ONLY

DepthOnlyVertexOutput main( VertexInputType input )
{
	DepthOnlyVertexOutput output;
#ifdef SKINNED
	// Тот же путь точки, что у полного варианта: глубина совпадает
	input.position.xyz = skinPoint( skinMatrix( input.joints, input.weights ), input.position.xyz );
#endif
	WindFrame frame = (WindFrame)0;	// базис здесь не нужен
	output.position = mul( mul( vertexWorldPosition( input, frame ), cb_viewMatrix ), cb_projectionMatrix );
	output.tex = input.tex;
#ifdef LOD_DITHER
	output.lodDither = lodDither( instanceSlot( input.instanceIndex ) );
#endif
	return output;
}

#else

PixelInputType main(VertexInputType input)
{
    PixelInputType output;

	output.tex = input.tex;
#ifdef SKINNED
	// Поза кадра — в координатах модели, дальше как у неподвижной модели
	const float3x4 skin = skinMatrix( input.joints, input.weights );
	input.position.xyz = skinPoint( skin, input.position.xyz );
	input.normal = skinDirection( skin, input.normal );
	input.tangent = skinDirection( skin, input.tangent );
	input.binormal = skinDirection( skin, input.binormal );
#endif

	const uint instanceIndex = instanceSlot( input.instanceIndex );
	output.instanceIndex = instanceIndex;
#ifdef LOD_DITHER
	output.lodDither = lodDither( instanceIndex );
#endif

	// Базис вершины в мире — до ветра: касательная и бинормаль лежат в поверхности и преобразуются как точки, нормаль —
	// обратной транспонированной; ветер гнёт его вместе с вершиной (vertexWorldPosition)
	float4x4 worldMatrix = objectWorldMatrix( instanceIndex );
	float3x3 normalMatrix = (float3x3)cb_worldInverseTransposeMatrix;
#ifdef INST_MATRIX
	normalMatrix = (float3x3)g_instanceTransforms[instanceIndex].worldInverseTranspose;
#endif
	float3 normal = input.normal;
	float3 tangent = input.tangent;
	float3 binormal = input.binormal;
	#if defined(INSTANCE_INCLUDE)
		normal = calcInstanceDirection( normal, instanceIndex );
		tangent = calcInstanceDirection( tangent, instanceIndex );
		binormal = calcInstanceDirection( binormal, instanceIndex );
	#endif
	WindFrame frame;
	frame.normal = normalize( mul( normal, normalMatrix ) );
	frame.tangent = normalize( mul( tangent, (float3x3)worldMatrix ) );
	frame.binormal = normalize( mul( binormal, (float3x3)worldMatrix ) );

	float4 worldPosition = vertexWorldPosition( input, frame );
	output.worldPosition = worldPosition.xyz;
#ifdef INST_SCATTER
	// Высота вершины над корнем экземпляра — в покое (без ветра): у корня цвет земли, к высоте перехода — свой
	const InstanceParam instance = g_instanceData[instanceIndex];
	scatterGround( instance.ground, max( input.position.y * instance.scale, 0.0f ), output.ground, output.groundBrightness );
#else
	output.ground = 0.0f;
	output.groundBrightness = 1.0f;
#endif
    output.position = mul( mul( worldPosition, cb_viewMatrix ), cb_projectionMatrix );
	output.normal = frame.normal;
	output.tangent = frame.tangent;
	output.binormal = frame.binormal;


    return output;
}

#endif
