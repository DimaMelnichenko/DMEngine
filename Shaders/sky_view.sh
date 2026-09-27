////////////////////////////////////////////////////////////////////////////////
// Небо вокруг наблюдателя — таблица Sky-View (Hillaire 2020, раздел 5.3; Sky-View LUT в Sky Atmosphere UE5),
// SKY_VIEW_LUT_WIDTH × HEIGHT, каждый кадр (Shaders/sky_view.ps). u — угол между азимутами взгляда и солнца (небо
// симметрично относительно вертикальной плоскости солнца; корень сгущает текселы у солнца), v — зенитный угол:
// верхняя половина — выше горизонта, нижняя — ниже, оба отображения сгущают текселы к горизонту. Яркость — на 1 лк
// солнца, с g_skyIntensity и цветностью солнца. skyLuminance — яркость неба в направлении для фона кадра
// (Shaders/sky_background.ps) и cubemap освещения окружением (Shaders/sky_cube.ps): таблица плюс земля под горизонтом
////////////////////////////////////////////////////////////////////////////////

#ifndef SKY_VIEW_SH
#define SKY_VIEW_SH

#include "atmosphere.sh"

Texture2D<float4> g_skyViewLut : register( t3 );
static const float2 skyViewLutSize = float2( SKY_VIEW_LUT_WIDTH, SKY_VIEW_LUT_HEIGHT );

// Зенитный угол горизонта наблюдателя (чуть больше π/2) и β — угол от горизонта до надира
void skyViewHorizon( out float horizonZenith, out float beta )
{
	beta = acos( horizonDistance( observerAltitude ) / ( planetRadius + observerAltitude ) );
	horizonZenith = atmospherePi - beta;
}

// Косинус угла между азимутами направления и солнца; взгляд или солнце в зените — 1
float cosAzimuthToSun( float3 direction )
{
	const float lengths = length( direction.xz ) * length( g_sunDirection.xz );
	return lengths > 1e-6f ? clamp( dot( direction.xz, g_sunDirection.xz ) / lengths, -1.0f, 1.0f ) : 1.0f;
}

float2 skyViewLutUv( float3 direction )
{
	float horizonZenith, beta;
	skyViewHorizon( horizonZenith, beta );
	const float zenith = acos( clamp( direction.y, -1.0f, 1.0f ) );
	const float v = zenith < horizonZenith ? 0.5f * ( 1.0f - sqrt( saturate( 1.0f - zenith / horizonZenith ) ) ) :
											 0.5f + 0.5f * sqrt( saturate( ( zenith - horizonZenith ) / beta ) );
	const float u = sqrt( saturate( 0.5f - 0.5f * cosAzimuthToSun( direction ) ) );
	return lutUvFromUnit( float2( u, v ), skyViewLutSize );
}

// Обратное: направление в мире для центра текселя таблицы
float3 skyViewLutDirection( float2 uv )
{
	float horizonZenith, beta;
	skyViewHorizon( horizonZenith, beta );
	const float2 unit = lutUnitFromUv( uv, skyViewLutSize );

	float zenith;
	if( unit.y < 0.5f )
	{
		const float c = 1.0f - 2.0f * unit.y;
		zenith = horizonZenith * ( 1.0f - c * c );
	}
	else
	{
		const float c = 2.0f * unit.y - 1.0f;
		zenith = horizonZenith + beta * c * c;
	}
	const float cosAzimuth = 1.0f - 2.0f * unit.x * unit.x;
	const float sinAzimuth = sqrt( saturate( 1.0f - cosAzimuth * cosAzimuth ) );

	// Азимут отсчитывается от солнца: базис горизонтальной плоскости — на солнце и поперёк
	float2 toSun = g_sunDirection.xz;
	toSun = dot( toSun, toSun ) > 1e-12f ? normalize( toSun ) : float2( 1.0f, 0.0f );
	const float2 horizontal = toSun * cosAzimuth + float2( -toSun.y, toSun.x ) * sinAzimuth;
	const float sinZenith = sin( zenith );
	return float3( horizontal.x * sinZenith, cos( zenith ), horizontal.y * sinZenith );
}

float3 skyViewLuminance( float3 direction )
{
	return g_skyViewLut.SampleLevel( g_SamplerLinearClamp, skyViewLutUv( direction ), 0.0f ).rgb;
}

// Ламбертова земля под горизонтом в точке position: солнце через атмосферу и свет неба — среднее яркости по пяти
// направлениям из той же таблицы (зенит и четыре на высоте 30°) вместо интеграла по полусфере
float3 groundRadiance( float3 position )
{
	const float3 up = normalize( position );
	const float3 sun = g_sunColor * transmittanceToTop( position, g_sunDirection ) * saturate( dot( up, g_sunDirection ) ) / atmospherePi;
	float3 sky = skyViewLuminance( float3( 0.0f, 1.0f, 0.0f ) );
	[unroll] for( int k = 0; k < 4; ++k )
	{
		const float azimuth = k * 0.5f * atmospherePi;
		sky += skyViewLuminance( float3( 0.866f * cos( azimuth ), 0.5f, 0.866f * sin( azimuth ) ) );
	}
	return g_groundAlbedo * ( sun + sky / 5.0f );
}

// Яркость неба в направлении direction без солнечного диска (диск рисует только фон: прямой свет солнца уже даёт
// направленный источник, в освещении окружением он был бы учтён дважды); под горизонтом — ещё и земля
float3 skyLuminance( float3 direction )
{
	float3 color = skyViewLuminance( direction );
	const float3 origin = float3( 0.0f, planetRadius + observerAltitude, 0.0f );
	const float groundDistance = raySphere( origin, direction, planetRadius ).x;
	[branch] if( groundDistance > 0.0f )
	{
		// Пропускание до земли — отношение пропусканий до края по обратному лучу (Bruneton): из земли через
		// наблюдателя и из наблюдателя
		const float3 groundPosition = origin + direction * groundDistance;
		const float3 transmittance = transmittanceToTop( groundPosition, -direction ) /
									 max( transmittanceToTop( origin, -direction ), 1e-6f );
		color += saturate( transmittance ) * groundRadiance( groundPosition );
	}
	return color;
}

#endif
