////////////////////////////////////////////////////////////////////////////////
// Автоэкспозиция, шаг 2: новая экспозиция кадра (Eye Adaptation в UE). Auto Exposure Histogram — средняя log₂
// яркость между процентилями Low / High Percent гистограммы, из неё целевой EV100 (ограничен Min / Max EV100);
// текущий EV100 плавно идёт к цели со скоростью Speed Up (сцена стала ярче) или Speed Down (темнее).
// Manual — EV100 из настроек. Затем поправка Exposure Compensation и, у автоэкспозиции, поправка по кривой от EV100
// сцены (Exposure Compensation Curve в UE: ночь темнее дня). Пишет ExposureState: sceneExposure — прежняя
// экспозиция (с ней нарисован этот кадр), exposure — новая. Класс PostProcess
////////////////////////////////////////////////////////////////////////////////

#include "slots.h"
#define EXPOSURE_STATE_WRITE
#include "exposure.sh"
#include "bindless.sh"

#define HISTOGRAM_BINS 64
#define MAX_CURVE_KEYS 8	// PostProcess::maxCurveKeys

DM_SRV( ByteAddressBuffer, g_histogram, 0 );
DM_UAV( RWStructuredBuffer<ExposureState>, g_state, 0 );

// Раскладка — PostProcess::AdaptParameters
cbuffer AdaptBuffer : register( b4 )
{
	int   g_meteringMode;			// 0 — Manual, 1 — Auto Exposure Histogram
	float g_manualEV100;
	float g_exposureCompensation;	// EV
	float g_deltaTime;				// с
	float g_minEV100;
	float g_maxEV100;
	float g_lowPercent;				// доля 0…1: темнее — не в среднем
	float g_highPercent;			// доля 0…1: ярче — не в среднем
	float g_speedUp;				// скорость адаптации, 1/с
	float g_speedDown;
	float g_minLog2Luminance;		// диапазон гистограммы — как у exposure_histogram.cs
	float g_log2LuminanceRange;
	int   g_curveKeyCount;			// ключей кривой компенсации; 0 — без кривой
	float3 g_adaptPadding;
	float4 g_curveKeys[MAX_CURVE_KEYS / 2];	// по два ключа (EV100 сцены, поправка EV), по возрастанию EV100
};

float2 curveKey( int index )
{
	const float4 pair = g_curveKeys[index / 2];
	return index % 2 == 0 ? pair.xy : pair.zw;
}

// Поправка по кривой: между ключами — линейно, за крайними — значение крайнего, как у кривой UE
float compensationCurve( float ev100 )
{
	if( g_curveKeyCount == 0 )
		return 0.0f;
	float2 previous = curveKey( 0 );
	if( ev100 <= previous.x )
		return previous.y;
	[loop] for( int i = 1; i < g_curveKeyCount; ++i )
	{
		const float2 key = curveKey( i );
		if( ev100 <= key.x )
			return lerp( previous.y, key.y, ( ev100 - previous.x ) / max( key.x - previous.x, 1e-6f ) );
		previous = key;
	}
	return previous.y;
}

[numthreads( 1, 1, 1 )]
void main()
{
	ExposureState state = g_state[0];
	float ev100 = g_manualEV100;

	[branch] if( g_meteringMode == 1 )
	{
		uint total = 0;
		[loop] for( uint i = 0; i < HISTOGRAM_BINS; ++i )
			total += g_histogram.Load( i * 4 );

		float target = state.ev100;
		[branch] if( total > 0 )
		{
			// Среднее по корзинам в полосе процентилей; корзина на краю полосы входит своей долей
			const float low = total * g_lowPercent;
			const float high = total * g_highPercent;
			float cumulative = 0.0f;
			float sum = 0.0f;
			float weight = 0.0f;
			[loop] for( uint bin = 0; bin < HISTOGRAM_BINS; ++bin )
			{
				const float count = (float)g_histogram.Load( bin * 4 );
				const float inBand = max( min( cumulative + count, high ) - max( cumulative, low ), 0.0f );
				const float log2Luminance = g_minLog2Luminance + ( bin + 0.5f ) / HISTOGRAM_BINS * g_log2LuminanceRange;
				sum += inBand * log2Luminance;
				weight += inBand;
				cumulative += count;
			}
			if( weight > 0.0f )
				target = ev100FromLuminance( exp2( sum / weight ) );
		}
		target = clamp( target, g_minEV100, g_maxEV100 );

		// Экспоненциальное приближение к цели, как в UE
		const float speed = target > state.ev100 ? g_speedUp : g_speedDown;
		ev100 = lerp( state.ev100, target, 1.0f - exp( -g_deltaTime * speed ) );
	}

	// Кривая — по адаптированному EV100: поправка меняется так же плавно, как адаптация (в UE — по замеру до
	// сглаживания, в установившемся состоянии то же). В Manual экспозиция задана целиком
	const float curve = g_meteringMode == 1 ? compensationCurve( ev100 ) : 0.0f;

	state.sceneExposure = state.exposure;
	state.ev100 = ev100;
	state.exposure = exposureFromEV100( ev100 - g_exposureCompensation - curve );
	g_state[0] = state;
}
