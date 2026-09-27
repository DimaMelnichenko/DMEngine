////////////////////////////////////////////////////////////////////////////////
// Рассеянный свет окружения: проекция cubemap окружения (неба или панорамы) на сферические гармоники 2-го порядка
// (9 коэффициентов, Ramamoorthi и Hanrahan 2001) со свёрткой косинусом. Результат — коэффициенты для E(n)/π:
// рассеянный свет поверхности = альбедо × ambientIrradiance(n) (Shaders/ibl.sh). Одна группа потоков, класс SkyLight
////////////////////////////////////////////////////////////////////////////////

#include "cubemap.sh"

Texture2DArray<float4> g_sky : register(t0);	// грани cubemap окружения (один мип) как массив
RWStructuredBuffer<float4> g_irradianceSH : register(u0);

static const uint threadCount = 64;
groupshared float3 s_sh[threadCount][9];
groupshared float s_weight[threadCount];

[numthreads( threadCount, 1, 1 )]
void main( uint thread : SV_GroupIndex )
{
	uint width, height, faces;
	g_sky.GetDimensions( width, height, faces );

	float3 sh[9];
	[unroll] for( uint c = 0; c < 9; ++c )
		sh[c] = 0.0f;
	float totalWeight = 0.0f;

	const uint texelCount = width * height * 6;
	[loop] for( uint texel = thread; texel < texelCount; texel += threadCount )
	{
		uint face = texel / ( width * height );
		uint x = texel % width;
		uint y = ( texel / width ) % height;
		float2 uv = ( float2( x, y ) + 0.5f ) / float2( width, height );
		float3 n = cubeDirection( face, uv );

		// Телесный угол текселя грани (без общего множителя — он уйдёт при нормировке)
		float2 st = uv * 2.0f - 1.0f;
		float weight = 1.0f / pow( 1.0f + dot( st, st ), 1.5f );
		float3 color = g_sky.Load( int4( x, y, face, 0 ) ).rgb * weight;

		sh[0] += color * 0.282095f;
		sh[1] += color * 0.488603f * n.y;
		sh[2] += color * 0.488603f * n.z;
		sh[3] += color * 0.488603f * n.x;
		sh[4] += color * 1.092548f * n.x * n.y;
		sh[5] += color * 1.092548f * n.y * n.z;
		sh[6] += color * 0.315392f * ( 3.0f * n.z * n.z - 1.0f );
		sh[7] += color * 1.092548f * n.x * n.z;
		sh[8] += color * 0.546274f * ( n.x * n.x - n.y * n.y );
		totalWeight += weight;
	}

	[unroll] for( uint s = 0; s < 9; ++s )
		s_sh[thread][s] = sh[s];
	s_weight[thread] = totalWeight;
	GroupMemoryBarrierWithGroupSync();

	if( thread != 0 )
		return;

	float3 result[9];
	float weightSum = 0.0f;
	[unroll] for( uint r = 0; r < 9; ++r )
		result[r] = 0.0f;
	[loop] for( uint t = 0; t < threadCount; ++t )
	{
		[unroll] for( uint k = 0; k < 9; ++k )
			result[k] += s_sh[t][k];
		weightSum += s_weight[t];
	}

	// Нормировка на полный телесный угол 4π и свёртка косинусом (A0 = π, A1 = 2π/3, A2 = π/4), делённая на π
	const float normalization = 4.0f * cubemapPi / weightSum;
	const float band[9] = { 1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
	[unroll] for( uint w = 0; w < 9; ++w )
		g_irradianceSH[w] = float4( result[w] * normalization * band[w], 0.0f );
}
