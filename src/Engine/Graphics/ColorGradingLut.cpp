#include "ColorGradingLut.h"
#include "Shaders\color_grading_constants.h"
#include "D3D\DMD3D.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace GS
{

namespace
{

// Нейтральный баланс белого: свет этой температуры остаётся белым
constexpr float neutralTemperature = 6500.0f;
// Оттенок 1 — сдвиг цветности источника на 0,05 поперёк линии чёрного тела (Duv в CIE 1960)
constexpr double tintScale = 0.05;

struct Chromaticity
{
	double u;
	double v;
};

// Цветность чёрного тела в CIE 1960 (u, v) — приближение Krystek 1985 (1000…15000 К)
Chromaticity planckianLocus( double temperature )
{
	const double t = temperature;
	const double t2 = t * t;
	return { ( 0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t2 ) / ( 1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t2 ),
			 ( 0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t2 ) / ( 1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t2 ) };
}

// Белая точка источника: на линии чёрного тела, со сдвигом поперёк неё на tint × tintScale (+ — к зелёному: такой свет
// баланс делает белым, и кадр становится пурпурнее), в XYZ с Y = 1
XMVECTOR sourceWhite( double temperature, double tint )
{
	Chromaticity uv = planckianLocus( temperature );
	const Chromaticity next = planckianLocus( temperature + 1.0 );
	double normalU = -( next.v - uv.v );
	double normalV = next.u - uv.u;
	if( normalV < 0.0 )
	{
		normalU = -normalU;
		normalV = -normalV;
	}
	const double length = std::sqrt( normalU * normalU + normalV * normalV );
	if( length > 0.0 )
	{
		uv.u += normalU / length * tint * tintScale;
		uv.v += normalV / length * tint * tintScale;
	}
	// (u, v) 1960 → (x, y) → XYZ
	const double d = 2.0 * uv.u - 8.0 * uv.v + 4.0;
	const double x = 3.0 * uv.u / d;
	const double y = 2.0 * uv.v / d;
	return XMVectorSet( static_cast<float>( x / y ), 1.0f, static_cast<float>( ( 1.0 - x - y ) / y ), 0.0f );
}

// Матрица баланса белого в линейном sRGB (как White Balance в UE): свет температуры temperature с оттенком tint становится
// белым — хроматическая адаптация Bradford к белой точке нейтральной температуры
XMMATRIX whiteBalance( float temperature, float tint )
{
	if( temperature == neutralTemperature && tint == 0.0f )
		return XMMatrixIdentity();

	// Матрицы — для столбца (M · v), произведения — обычные (XMMatrixMultiply( A, B ) = A · B)
	const XMMATRIX sRGBToXYZ( 0.4123907993f, 0.3575843394f, 0.1804807884f, 0.0f,
							  0.2126390059f, 0.7151686788f, 0.0721923154f, 0.0f,
							  0.0193308187f, 0.1191947798f, 0.9505321522f, 0.0f,
							  0.0f, 0.0f, 0.0f, 1.0f );
	const XMMATRIX bradford( 0.8951f, 0.2664f, -0.1614f, 0.0f,
							 -0.7502f, 1.7135f, 0.0367f, 0.0f,
							 0.0389f, -0.0685f, 1.0296f, 0.0f,
							 0.0f, 0.0f, 0.0f, 1.0f );
	// Отклики колбочек на белые точки: источника и нейтральную
	const auto cone = [&]( XMVECTOR white )
	{
		return XMVectorSet( XMVectorGetX( XMVector3Dot( bradford.r[0], white ) ), XMVectorGetX( XMVector3Dot( bradford.r[1], white ) ),
							XMVectorGetX( XMVector3Dot( bradford.r[2], white ) ), 1.0f );
	};
	const XMVECTOR source = cone( sourceWhite( std::clamp( temperature, 1000.0f, 15000.0f ), tint ) );
	const XMVECTOR target = cone( sourceWhite( neutralTemperature, 0.0 ) );
	const XMMATRIX scale = XMMatrixScalingFromVector( XMVectorDivide( target, source ) );
	const XMMATRIX adaptation = XMMatrixMultiply( XMMatrixInverse( nullptr, bradford ), XMMatrixMultiply( scale, bradford ) );
	return XMMatrixMultiply( XMMatrixInverse( nullptr, sRGBToXYZ ), XMMatrixMultiply( adaptation, sRGBToXYZ ) );
}

// Значение части × общее: каналы × общий множитель (w)
XMFLOAT4 combined( const XMFLOAT4& global, const XMFLOAT4& part, float minimum )
{
	const auto channel = [&]( float a, float b ) { return std::max( a * global.w * b * part.w, minimum ); };
	return XMFLOAT4( channel( global.x, part.x ), channel( global.y, part.y ), channel( global.z, part.z ), 0.0f );
}

XMFLOAT4 combinedOffset( const XMFLOAT4& global, const XMFLOAT4& part )
{
	const float shift = global.w + part.w;
	return XMFLOAT4( global.x + part.x + shift, global.y + part.y + shift, global.z + part.z + shift, 0.0f );
}

}

bool ColorGradingLut::initialize()
{
	if( !m_shader.initialize( "Shaders\\color_grading_lut.cs", "main" ) )
		return false;

	DMD3D& d3d = DMD3D::instance();
	// Цвет экрана в кодировке sRGB: половинной точности хватает (у 1,0 шаг — 1/2048)
	TextureDesc desc;
	desc.width = size;
	desc.height = size;
	desc.depth = size;
	desc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	if( !d3d.createTexture( desc, nullptr, m_texture ) || !d3d.createStorageView( m_texture, {}, m_uav ) ||
		!d3d.createShaderView( m_texture, {}, m_srv ) || !d3d.createShaderConstantBuffer( sizeof( ColorGradingBuffer ), m_constants ) )
		return false;
	d3d.setName( m_texture, "Color grading LUT" );
	return true;
}

void ColorGradingLut::update( const ColorGradingSettings& settings, Tonemapper tonemapper, uint64_t revision, GpuProfiler& profiler )
{
	if( revision == m_revision )
		return;
	m_revision = revision;

	ColorGradingBuffer params = {};
	const XMMATRIX balance = whiteBalance( settings.whiteTemp, settings.whiteTint );
	for( uint32_t row = 0; row < 3; ++row )
		XMStoreFloat4( &params.whiteBalance[row], XMVectorSetW( balance.r[row], 0.0f ) );
	// Тени, полутона, света: значения части × общее; контраст и гамма — не 0 (степень и деление на гамму)
	const ColorGradingPart* parts[] = { &settings.shadows, &settings.midtones, &settings.highlights };
	for( uint32_t i = 0; i < 3; ++i )
	{
		params.saturation[i] = combined( settings.global.saturation, parts[i]->saturation, 0.0f );
		params.contrast[i] = combined( settings.global.contrast, parts[i]->contrast, 1e-3f );
		params.gamma[i] = combined( settings.global.gamma, parts[i]->gamma, 1e-3f );
		params.gain[i] = combined( settings.global.gain, parts[i]->gain, 0.0f );
		params.offset[i] = combinedOffset( settings.global.offset, parts[i]->offset );
	}
	// Границы частей — не нулевой ширины (smoothstep делит на неё)
	params.shadowsMax = std::max( settings.shadowsMax, 1e-4f );
	params.highlightsMin = std::max( settings.highlightsMin, 0.0f );
	params.highlightsMax = std::max( settings.highlightsMax, params.highlightsMin + 1e-4f );
	params.tonemapper = static_cast<int32_t>( tonemapper );

	profiler.beginScope( "Color grading LUT" );
	DMD3D& d3d = DMD3D::instance();
	d3d.beginPass( PassDesc{ "Color grading LUT", {}, {}, 0, 0, {}, { { &m_uav, "color grading LUT" } } } );
	Device::updateResourceData<ColorGradingBuffer>( m_constants, params );
	d3d.setConstantBuffer( 4, m_constants );
	m_shader.setUAVBuffer( 0, m_uav );
	// Поток — ячейка, группа — 4 × 4 × 4
	m_shader.dispatchGroups( size / 4, size / 4, size / 4 );
	profiler.endScope();
}

}
