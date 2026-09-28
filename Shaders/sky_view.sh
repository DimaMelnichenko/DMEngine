////////////////////////////////////////////////////////////////////////////////
// Небо вокруг наблюдателя — таблицы Sky-View (Hillaire 2020, раздел 5.3; Sky-View LUT в Sky Atmosphere UE5),
// SKY_VIEW_LUT_WIDTH × HEIGHT, каждый кадр (Shaders/sky_view.ps), по таблице на светило: солнце и луна. u — угол между
// азимутами взгляда и светила (небо от одного светила симметрично относительно его вертикальной плоскости; корень
// сгущает текселы у светила), v — зенитный угол: верхняя половина — выше горизонта, нижняя — ниже, оба отображения
// сгущают текселы к горизонту. Яркость — в единицах запекания (на 1 лк солнца), с g_skyIntensity и цветом светила.
// skyLuminance — яркость неба в направлении для фона кадра (Shaders/sky_background.ps) и cubemap освещения окружением
// (Shaders/sky_cube.ps): таблицы, свечение ночного неба и земля под горизонтом
////////////////////////////////////////////////////////////////////////////////

#ifndef SKY_VIEW_SH
#define SKY_VIEW_SH

#include "atmosphere.sh"

Texture2D<float4> g_skyViewLut : register( t3 );		// солнце
Texture2D<float4> g_skyViewMoonLut : register( t4 );	// луна — только если она есть (g_moonColor > 0)
static const float2 skyViewLutSize = float2( SKY_VIEW_LUT_WIDTH, SKY_VIEW_LUT_HEIGHT );

// Свечение ночного неба (airglow, суммарный свет звёзд, зодиакальный свет) — слой на высоте ~100–300 км: к горизонту
// взгляд идёт в нём дольше (эффект ван Рейна), высота слоя — ~300 км
static const float nightGlowLayerHeight = 300000.0f;

bool moonInSky()
{
	return any( g_moonColor > 0.0f );
}

// Зенитный угол горизонта наблюдателя (чуть больше π/2) и β — угол от горизонта до надира
void skyViewHorizon( out float horizonZenith, out float beta )
{
	beta = acos( horizonDistance( observerAltitude ) / ( planetRadius + observerAltitude ) );
	horizonZenith = atmospherePi - beta;
}

// Косинус угла между азимутами направления и светила; взгляд или светило в зените — 1
float cosAzimuthToLight( float3 direction, float3 lightDirection )
{
	const float lengths = length( direction.xz ) * length( lightDirection.xz );
	return lengths > 1e-6f ? clamp( dot( direction.xz, lightDirection.xz ) / lengths, -1.0f, 1.0f ) : 1.0f;
}

float2 skyViewLutUv( float3 direction, float3 lightDirection )
{
	float horizonZenith, beta;
	skyViewHorizon( horizonZenith, beta );
	const float zenith = acos( clamp( direction.y, -1.0f, 1.0f ) );
	const float v = zenith < horizonZenith ? 0.5f * ( 1.0f - sqrt( saturate( 1.0f - zenith / horizonZenith ) ) ) :
											 0.5f + 0.5f * sqrt( saturate( ( zenith - horizonZenith ) / beta ) );
	const float u = sqrt( saturate( 0.5f - 0.5f * cosAzimuthToLight( direction, lightDirection ) ) );
	return lutUvFromUnit( float2( u, v ), skyViewLutSize );
}

// Обратное: направление в мире для центра текселя таблицы светила lightDirection
float3 skyViewLutDirection( float2 uv, float3 lightDirection )
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

	// Азимут отсчитывается от светила: базис горизонтальной плоскости — на светило и поперёк
	float2 toLight = lightDirection.xz;
	toLight = dot( toLight, toLight ) > 1e-12f ? normalize( toLight ) : float2( 1.0f, 0.0f );
	const float2 horizontal = toLight * cosAzimuth + float2( -toLight.y, toLight.x ) * sinAzimuth;
	const float sinZenith = sin( zenith );
	return float3( horizontal.x * sinZenith, cos( zenith ), horizontal.y * sinZenith );
}

float3 skyViewLuminance( float3 direction )
{
	float3 luminance = g_skyViewLut.SampleLevel( g_SamplerLinearClamp, skyViewLutUv( direction, g_sunDirection ), 0.0f ).rgb;
	[branch] if( moonInSky() )
		luminance += g_skyViewMoonLut.SampleLevel( g_SamplerLinearClamp, skyViewLutUv( direction, g_moonDirection ), 0.0f ).rgb;
	return luminance;
}

// Свечение ночного неба выше горизонта: в зените g_nightSkyLuminance, к горизонту ярче по толщине слоя свечения
// (ван Рейн: 1 / √(1 − (R / (R + h))² sin² z)), с пропусканием атмосферы по лучу взгляда
float3 nightSkyGlow( float3 direction )
{
	const float ratio = planetRadius / ( planetRadius + nightGlowLayerHeight );
	const float sinZenith2 = saturate( 1.0f - direction.y * direction.y );
	const float vanRhijn = rsqrt( max( 1.0f - ratio * ratio * sinZenith2, 1e-4f ) );
	return g_nightSkyLuminance * vanRhijn * transmittanceToTop( observerAltitude, direction.y );
}

// Ламбертова земля под горизонтом в точке position: солнце через атмосферу и свет неба — среднее яркости по пяти
// направлениям из той же таблицы (зенит и четыре на высоте 30°) вместо интеграла по полусфере
float3 groundRadiance( float3 position )
{
	const float3 up = normalize( position );
	float3 sun = g_sunColor * transmittanceToTop( position, g_sunDirection ) * saturate( dot( up, g_sunDirection ) ) / atmospherePi;
	[branch] if( moonInSky() )
		sun += g_moonColor * transmittanceToTop( position, g_moonDirection ) * saturate( dot( up, g_moonDirection ) ) / atmospherePi;
	float3 sky = skyViewLuminance( float3( 0.0f, 1.0f, 0.0f ) );
	[unroll] for( int k = 0; k < 4; ++k )
	{
		const float azimuth = k * 0.5f * atmospherePi;
		sky += skyViewLuminance( float3( 0.866f * cos( azimuth ), 0.5f, 0.866f * sin( azimuth ) ) );
	}
	return g_groundAlbedo * ( sun + sky / 5.0f );
}

// Яркость неба в направлении direction без дисков солнца и луны (их рисует только фон: прямой свет светил уже дают
// направленные источники, в освещении окружением он был бы учтён дважды) и без звёзд; со свечением ночного неба; под
// горизонтом — ещё и земля
float3 skyLuminance( float3 direction )
{
	float3 color = skyViewLuminance( direction ) + nightSkyGlow( direction );
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
