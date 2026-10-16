////////////////////////////////////////////////////////////////////////////////
// Таблица цветокоррекции и тонмаппинга COLOR_GRADING_LUT_SIZE³ (класс ColorGradingLut; как Combine LUTs в UE): ячейка —
// цвет после экспозиции (color_grading.sh) → баланс белого → цветокоррекция по теням, полутонам и светам в ACEScg (AP1,
// как Color Grading в UE) → кривая тонмаппинга → цвет экрана в кодировке sRGB. Строится заново при правке окна
// «Post process»; tonemap.ps берёт из неё одну выборку на пиксель
////////////////////////////////////////////////////////////////////////////////

#include "slots.h"
#include "bindless.sh"
#include "color_grading.sh"

DM_UAV( RWTexture3D<float4>, g_lut, 0 );

#include "color_grading_constants.h"

// Линейный sRGB (Rec.709, D65) ↔ ACEScg (AP1, D60; белая точка — Bradford): цветокоррекция — в широком охвате, как в UE
// (насыщенность и контраст в нём не уводят яркие цвета за охват и меньше смещают оттенок)
static const float3x3 sRGBToAP1 =
{
	0.6130974024f, 0.3395231462f, 0.0473794514f,
	0.0701937225f, 0.9163538791f, 0.0134523985f,
	0.0206155929f, 0.1095697729f, 0.8698146342f
};

static const float3x3 AP1ToSRGB =
{
	 1.7050509927f, -0.6217921207f, -0.0832588720f,
	-0.1302564175f,  1.1408047366f, -0.0105483191f,
	-0.0240033568f, -0.1289689761f,  1.1529723329f
};

// Яркость Y в AP1 — строка AP1 → XYZ
static const float3 AP1Luminance = float3( 0.2722287168f, 0.6740817658f, 0.0536895174f );

// ACES: подгонка RRT + ODT (Stephen Hill, BakingLab, лицензия MIT). Вход и выход — линейный sRGB
static const float3x3 acesInput =
{
	0.59719f, 0.35458f, 0.04823f,
	0.07600f, 0.90834f, 0.01566f,
	0.02840f, 0.13383f, 0.83777f
};

static const float3x3 acesOutput =
{
	 1.60475f, -0.53108f, -0.07367f,
	-0.10208f,  1.10813f, -0.00605f,
	-0.00327f, -0.07276f,  1.07602f
};

float3 acesFitted( float3 color )
{
	color = mul( acesInput, color );
	float3 a = color * ( color + 0.0245786f ) - 0.000090537f;
	float3 b = color * ( 0.983729f * color + 0.4329510f ) + 0.238081f;
	return saturate( mul( acesOutput, a / b ) );
}

// AgX: приближение отображения Blender 4+ (Benjamin Wrensch, «Minimal AgX», лицензия MIT): вход в «рабочее»
// пространство AgX, логарифм в диапазоне 16,5 EV, сигмоида полиномом, обратная матрица и гамма дисплея 2,2
static const float3x3 agxInset =
{
	0.842479062253094f, 0.0784335999999992f, 0.0792237451477643f,
	0.0423282422610123f, 0.878468636469772f, 0.0791661274605434f,
	0.0423756549057051f, 0.0784336f, 0.879142973793104f
};

static const float3x3 agxOutset =
{
	 1.19687900512017f, -0.0980208811401368f, -0.0990297440797205f,
	-0.0528968517574562f, 1.15190312990417f, -0.0989611768448433f,
	-0.0529716355144438f, -0.0980434501171241f, 1.15107367264116f
};

float3 agxContrast( float3 x )
{
	float3 x2 = x * x;
	float3 x4 = x2 * x2;
	return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
}

float3 agx( float3 color )
{
	const float minEv = -12.47393f;
	const float maxEv = 4.026069f;

	color = mul( agxInset, max( color, 1e-10f ) );
	color = ( clamp( log2( color ), minEv, maxEv ) - minEv ) / ( maxEv - minEv );
	color = agxContrast( color );
	color = mul( agxOutset, color );
	// Результат закодирован для дисплея с гаммой 2,2; дальше ждут линейное значение
	return pow( saturate( color ), 2.2f );
}

// Коррекция одной части (тени, полутона, света), как в Color Grading UE: насыщенность — от серого той же яркости, контраст —
// вокруг среднего серого 0,18, гамма, усиление и сдвиг
float3 colorCorrect( float3 color, float luminance, uint part )
{
	color = max( lerp( luminance, color, g_saturation[part].rgb ), 0.0f );
	color = pow( color * ( 1.0f / 0.18f ), g_contrast[part].rgb ) * 0.18f;
	color = pow( color, 1.0f / g_gamma[part].rgb );
	return color * g_gain[part].rgb + g_offset[part].rgb;
}

[numthreads( 4, 4, 4 )]
void main( uint3 id : SV_DispatchThreadID )
{
	if( any( id >= COLOR_GRADING_LUT_SIZE ) )
		return;

	float3 color = colorGradingCellColor( id );
	color = float3( dot( g_whiteBalance[0].xyz, color ), dot( g_whiteBalance[1].xyz, color ), dot( g_whiteBalance[2].xyz, color ) );

	// Доли частей — по яркости до коррекции; полутона — остаток (если тени и света перекрылись — делятся поровну)
	color = mul( sRGBToAP1, color );
	const float luminance = dot( color, AP1Luminance );
	const float shadows = 1.0f - smoothstep( 0.0f, g_shadowsMax, luminance );
	const float highlights = smoothstep( g_highlightsMin, g_highlightsMax, luminance );
	const float midtones = max( 1.0f - shadows - highlights, 0.0f );
	color = ( colorCorrect( color, luminance, 0 ) * shadows + colorCorrect( color, luminance, 1 ) * midtones +
			  colorCorrect( color, luminance, 2 ) * highlights ) / ( shadows + midtones + highlights );
	color = max( mul( AP1ToSRGB, color ), 0.0f );

	[branch] if( g_tonemapper == 1 )
		color = acesFitted( color );
	else if( g_tonemapper == 2 )
		color = agx( color );
	else
		color = saturate( color );
	g_lut[id] = float4( linearToSRGB( color ), 1.0f );
}
