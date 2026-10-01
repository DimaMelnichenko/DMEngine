////////////////////////////////////////////////////////////////////////////////
// Импостер (ImpostorMaterial, Shaders/impostor.sh): экземпляр расстановки (INST_POS, INST_SCALE, INST_ROTATE) —
// карточка в плоскости вида через центр сферы модели, размером в её диаметр. Кадры — три ближайших к направлению на
// зрителя в координатах модели (у перспективного вида — к камере, у ортографического каскада теней — против света):
// UV точки карточки в каждом — её проекция на плоскость кадра. С LOD_DITHER — доля смены LOD экземпляра
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "instance.sh"
#include "impostor.sh"

struct VertexInputType
{
	float4 position : POSITION0;
	float2 tex : TEXCOORD0;
	uint instanceIndex : SV_InstanceID;
};

// UV точки offset (координаты модели, от центра сферы) в кадре frame
float2 impostorFrameUV( uint2 frame, float3 offset )
{
	float3 right, up;
	impostorFrameBasis( impostorFrameDirection( frame ), right, up );
	const float radius = g_impostorBounds.w;
	return float2( 0.5f + 0.5f * dot( offset, right ) / radius, 0.5f - 0.5f * dot( offset, up ) / radius );
}

ImpostorPixelInput main( VertexInputType input )
{
	const uint slot = instanceSlot( input.instanceIndex );
	const InstanceParam instance = g_instanceData[slot];
	const float scale = instance.scale;
	const float3 center = instance.position + impostorRotate( g_impostorBounds.xyz, instance.rotation ) * scale;
	const float radius = g_impostorBounds.w * scale;

	const bool orthographic = cb_projectionMatrix[3][3] > 0.5f;
	const float3 toViewer = orthographic ? -cb_viewDirection : normalize( cb_cameraPosition - center );

	// Карточка — в плоскости вида: углы по осям вида; UV карточки (0…1, v вниз) → (−1…1, вверх)
	const float3 right = normalize( cb_viewInverseMatrix[0].xyz );
	const float3 up = normalize( cb_viewInverseMatrix[1].xyz );
	const float2 corner = float2( input.tex.x * 2.0f - 1.0f, 1.0f - input.tex.y * 2.0f );
	const float3 worldPosition = center + ( right * corner.x + up * corner.y ) * radius;

	// Направление на зрителя и точка карточки — в координатах модели (без поворота и масштаба экземпляра); снизу кадров
	// нет — направление прижимается к горизонту
	const float4 inverse = float4( -instance.rotation.xyz, instance.rotation.w );
	float3 viewDirection = impostorRotate( toViewer, inverse );
	viewDirection.y = max( viewDirection.y, 1e-3f );
	viewDirection = normalize( viewDirection );
	const float3 offset = impostorRotate( worldPosition - center, inverse ) / max( scale, 1e-4f );

	// Три кадра вокруг направления: треугольник сетки, в котором оно лежит, и барицентрические веса
	const uint frames = g_impostorFrames;
	const float2 grid = ( hemiOctEncode( viewDirection ) * 0.5f + 0.5f ) * float( frames - 1 );
	const uint2 base = min( (uint2)max( floor( grid ), 0.0f ), frames - 2 );
	const float2 f = saturate( grid - float2( base ) );
	uint2 frame0, frame1, frame2;
	float3 weights;
	if( f.x + f.y < 1.0f )
	{
		frame0 = base;
		frame1 = base + uint2( 1, 0 );
		frame2 = base + uint2( 0, 1 );
		weights = float3( 1.0f - f.x - f.y, f.x, f.y );
	}
	else
	{
		frame0 = base + uint2( 1, 1 );
		frame1 = base + uint2( 0, 1 );
		frame2 = base + uint2( 1, 0 );
		weights = float3( f.x + f.y - 1.0f, 1.0f - f.x, 1.0f - f.y );
	}

	ImpostorPixelInput output;
	output.position = mul( float4( worldPosition, 1.0f ), cb_viewProjectionMatrix );
	output.worldPosition = worldPosition;
	output.frameUV01 = float4( impostorFrameUV( frame0, offset ), impostorFrameUV( frame1, offset ) );
	output.frameUV2 = impostorFrameUV( frame2, offset );
	output.frames = uint3( frame0.y * frames + frame0.x, frame1.y * frames + frame1.x, frame2.y * frames + frame2.x );
	output.weights = weights;
	output.rotation = instance.rotation;
#ifdef LOD_DITHER
	output.lodDither = instance.lodDither;
#endif
	return output;
}
