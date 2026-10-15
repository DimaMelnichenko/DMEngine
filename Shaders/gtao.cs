////////////////////////////////////////////////////////////////////////////////
// Экранное затенение окружением — GTAO (Jimenez, Wu, Pesce, Jarabo 2016, «Practical Real-Time Strategies for Accurate
// Indirect Occlusion»; устройство — как XeGTAO у Intel и GTAO в UE5), AmbientOcclusion. По глубине depth prepass, в
// половине кадра: точка — пиксель полного кадра (2x, 2y), её нормаль — по соседям в глубине. Вокруг точки — несколько
// срезов полусферы плоскостями через направление взгляда; в каждом срезе по обе стороны ищется горизонт (наибольший
// косинус угла к направлению на камеру по точкам глубины на расстоянии до радиуса), и видимая доля неба между двумя
// горизонтами считается аналитически с косинусом к нормали. Срезы и шаги сдвинуты шумом 4 × 4 (без временного
// сглаживания — только пространственный), mainDenoise сглаживает его окном 5 × 5 с весами по глубине: окно
// покрывает все 16 сдвигов шума поровну. Результат — видимость 0…1 и глубина вида точки (по ней материалы берут
// затенение из половины кадра без смешивания с другой поверхностью, Shaders/ambient_occlusion.sh)
////////////////////////////////////////////////////////////////////////////////

#include "bindless.sh"

#include "ambient_occlusion_constants.h"

DM_SRV( Texture2D<float>, g_depth, 0 );		// глубина сцены (обратная: 1 — у камеры, 0 — небо)
DM_SRV( Texture2D<float2>, g_noisy, 1 );		// mainDenoise: результат main
DM_UAV( RWTexture2D<float2>, g_output, 0 );	// x — видимость 0…1, y — глубина вида, м

static const float PI = 3.14159265f;
static const float HALF_PI = 1.57079633f;
static const uint sliceCount = 3;
static const uint stepsPerSide = 4;
static const float sampleDistributionPower = 2.0f;	// шаги гуще у точки
static const float tooCloseThreshold = 1.3f;		// пикселей: соседний пиксель — ещё сама точка
static const float skyDepth = 1e6f;

// Глубина вида в пикселе полного кадра, м; небо (глубина 0) — очень далеко
float viewDepth( int2 pixel )
{
	const float d = g_depth.Load( int3( clamp( pixel, int2( 0, 0 ), int2( g_fullSize ) - 1 ), 0 ) );
	return d <= 0.0f ? skyDepth : g_depthLinearize.y / ( d - g_depthLinearize.x );
}

// Точка вида пикселя полного кадра (центр пикселя): x вправо, y вверх, z — вперёд
float3 viewPosition( float2 pixelCenter, float z )
{
	const float2 ndc = float2( pixelCenter.x / g_fullSize.x * 2.0f - 1.0f, 1.0f - pixelCenter.y / g_fullSize.y * 2.0f );
	return float3( ndc * g_tanHalfFov * z, z );
}

// Края по глубине соседей (как XeGTAO_CalculateEdges): 1 — сосед на той же поверхности, с поправкой на наклон
float4 depthEdges( float center, float left, float right, float top, float bottom )
{
	float4 edges = float4( left, right, top, bottom ) - center;
	const float slopeLR = ( edges.y - edges.x ) * 0.5f;
	const float slopeTB = ( edges.w - edges.z ) * 0.5f;
	const float4 adjusted = edges + float4( slopeLR, -slopeLR, slopeTB, -slopeTB );
	edges = min( abs( edges ), abs( adjusted ) );
	return saturate( 1.25f - edges / ( center * 0.011f ) );
}

// Шум 4 × 4: номер 0…15 в блоке (Байер), из него — сдвиг срезов и шагов последовательностью R2 (каждый блок 4 × 4
// содержит все 16 сочетаний — фильтр 5 × 5 их усредняет)
float2 blockNoise( uint2 pixel )
{
	static const uint bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
	const float index = (float)bayer[( pixel.y & 3 ) * 4 + ( pixel.x & 3 )];
	return frac( 0.5f + index * float2( 0.7548776662f, 0.5698402910f ) );
}

float fastAcos( float x )
{
	const float a = abs( x );
	float result = -0.156583f * a + HALF_PI;
	result *= sqrt( 1.0f - a );
	return x >= 0.0f ? result : PI - result;
}

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_halfSize ) )
		return;
	const int2 pixel = int2( id.xy ) * 2;
	const float centerZ = viewDepth( pixel );
	if( centerZ >= skyDepth )
	{
		g_output[id.xy] = float2( 1.0f, skyDepth );
		return;
	}
	const float2 centerPixel = float2( pixel ) + 0.5f;
	const float3 position = viewPosition( centerPixel, centerZ );

	// Нормаль — по соседям в глубине с весами краёв (на краю объекта — по той стороне, что на той же поверхности)
	const float leftZ = viewDepth( pixel + int2( -1, 0 ) );
	const float rightZ = viewDepth( pixel + int2( 1, 0 ) );
	const float topZ = viewDepth( pixel + int2( 0, -1 ) );
	const float bottomZ = viewDepth( pixel + int2( 0, 1 ) );
	const float4 edges = depthEdges( centerZ, leftZ, rightZ, topZ, bottomZ );
	const float3 left = normalize( viewPosition( centerPixel + float2( -1.0f, 0.0f ), leftZ ) - position );
	const float3 right = normalize( viewPosition( centerPixel + float2( 1.0f, 0.0f ), rightZ ) - position );
	const float3 top = normalize( viewPosition( centerPixel + float2( 0.0f, -1.0f ), topZ ) - position );
	const float3 bottom = normalize( viewPosition( centerPixel + float2( 0.0f, 1.0f ), bottomZ ) - position );
	const float4 accepted = saturate( float4( edges.x * edges.z, edges.z * edges.y, edges.y * edges.w, edges.w * edges.x ) + 0.01f );
	float3 normal = normalize( accepted.x * cross( left, top ) + accepted.y * cross( top, right ) +
							   accepted.z * cross( right, bottom ) + accepted.w * cross( bottom, left ) );
	const float3 viewVector = normalize( -position );
	if( dot( normal, viewVector ) < 0.0f )
		normal = -normal;

	// Радиус на экране; дальше, где он меньше пары пикселей, затенения нет
	const float pixelRadius = min( g_radius * g_pixelsPerMeter / centerZ, g_maxPixelRadius );
	if( pixelRadius < 2.0f )
	{
		g_output[id.xy] = float2( 1.0f, centerZ );
		return;
	}
	const float minStep = tooCloseThreshold / pixelRadius;
	const float2 noise = blockNoise( id.xy );

	float visibility = 0.0f;
	[unroll] for( uint slice = 0; slice < sliceCount; ++slice )
	{
		const float phi = ( slice + noise.x ) / sliceCount * PI;
		const float cosPhi = cos( phi );
		const float sinPhi = sin( phi );
		// Направление по экрану (y вниз) и в виде (y вверх)
		const float2 omega = float2( cosPhi, -sinPhi ) * pixelRadius;
		const float3 direction = float3( cosPhi, sinPhi, 0.0f );
		const float3 orthoDirection = direction - dot( direction, viewVector ) * viewVector;
		const float3 axis = normalize( cross( orthoDirection, viewVector ) );
		const float3 projectedNormal = normal - axis * dot( normal, axis );
		const float signNormal = sign( dot( orthoDirection, projectedNormal ) );
		float projectedLength = length( projectedNormal );
		const float cosNormal = saturate( dot( projectedNormal, viewVector ) / max( projectedLength, 1e-4f ) );
		const float n = signNormal * fastAcos( cosNormal );
		// Горизонт без заслонов — касательная плоскость
		const float lowHorizon0 = cos( n + HALF_PI );
		const float lowHorizon1 = cos( n - HALF_PI );
		float horizon0 = lowHorizon0;
		float horizon1 = lowHorizon1;
		[unroll] for( uint step = 0; step < stepsPerSide; ++step )
		{
			const float stepNoise = frac( noise.y + float( slice + step * stepsPerSide ) * 0.6180339887f );
			float s = ( step + stepNoise ) / stepsPerSide;
			s = pow( s, sampleDistributionPower ) + minStep;
			const float2 offset = round( s * omega );
			const float2 pixel0 = centerPixel + offset;
			const float2 pixel1 = centerPixel - offset;
			const float3 delta0 = viewPosition( pixel0, viewDepth( int2( floor( pixel0 ) ) ) ) - position;
			const float3 delta1 = viewPosition( pixel1, viewDepth( int2( floor( pixel1 ) ) ) ) - position;
			const float distance0 = length( delta0 );
			const float distance1 = length( delta1 );
			// Точка за пределами радиуса заслоняет всё слабее (спад к краю), за краем кадра — не заслоняет
			const bool inside0 = all( pixel0 >= 0.0f ) && all( pixel0 < float2( g_fullSize ) );
			const bool inside1 = all( pixel1 >= 0.0f ) && all( pixel1 < float2( g_fullSize ) );
			const float weight0 = inside0 ? saturate( distance0 * g_falloffMul + g_falloffAdd ) : 0.0f;
			const float weight1 = inside1 ? saturate( distance1 * g_falloffMul + g_falloffAdd ) : 0.0f;
			const float cos0 = lerp( lowHorizon0, dot( delta0 / max( distance0, 1e-5f ), viewVector ), weight0 );
			const float cos1 = lerp( lowHorizon1, dot( delta1 / max( distance1, 1e-5f ), viewVector ), weight1 );
			horizon0 = max( horizon0, cos0 );
			horizon1 = max( horizon1, cos1 );
		}
		// Видимая доля между горизонтами с косинусом к нормали (интеграл по дуге среза)
		projectedLength = lerp( projectedLength, 1.0f, 0.05f );
		const float h0 = -fastAcos( horizon1 );
		const float h1 = fastAcos( horizon0 );
		const float arc0 = ( cosNormal + 2.0f * h0 * sin( n ) - cos( 2.0f * h0 - n ) ) / 4.0f;
		const float arc1 = ( cosNormal + 2.0f * h1 * sin( n ) - cos( 2.0f * h1 - n ) ) / 4.0f;
		visibility += projectedLength * ( arc0 + arc1 );
	}
	visibility = pow( saturate( visibility / sliceCount ), g_finalPower );
	g_output[id.xy] = float2( max( visibility, 0.03f ), centerZ );
}

// Фильтр: окно 5 × 5 с весами 0,5 / 1 / 1 / 1 / 0,5 по каждой оси (все 16 сдвигов шума — поровну) и по глубине:
// сосед с другой поверхности (край объекта) не смешивается
[numthreads( 8, 8, 1 )]
void mainDenoise( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_halfSize ) )
		return;
	const float2 center = g_noisy.Load( int3( id.xy, 0 ) );
	if( center.y >= skyDepth )
	{
		g_output[id.xy] = center;
		return;
	}
	static const float tent[5] = { 0.5f, 1.0f, 1.0f, 1.0f, 0.5f };
	float sum = 0.0f;
	float weightSum = 0.0f;
	[unroll] for( int y = -2; y <= 2; ++y )
	{
		[unroll] for( int x = -2; x <= 2; ++x )
		{
			const int2 coord = clamp( int2( id.xy ) + int2( x, y ), int2( 0, 0 ), int2( g_halfSize ) - 1 );
			const float2 value = g_noisy.Load( int3( coord, 0 ) );
			const float weight = tent[x + 2] * tent[y + 2] * saturate( 1.0f - abs( value.y - center.y ) / ( center.y * g_depthTolerance ) );
			sum += value.x * weight;
			weightSum += weight;
		}
	}
	g_output[id.xy] = float2( sum / max( weightSum, 1e-4f ), center.y );
}
