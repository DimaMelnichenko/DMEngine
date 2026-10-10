#include "WaterRipples.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>
#include <DirectXPackedVector.h>
#include <DirectXTex.h>
#include "D3D\TextureImages.h"

using namespace DirectX;

namespace
{

// Октава — разность гауссиан с радиусами sigma и 2 × sigma (тексели): пропускает длины волн около 6,5 × sigma
const float coarseSigma = 3.0f;
const float fineSigma = 1.1f;

// Гауссово размытие по строкам и столбцам с переходом через край (карта бесшовная); size — степень двойки
std::vector<float> blur( const std::vector<float>& source, uint32_t size, float sigma )
{
	const int radius = static_cast<int>( std::ceil( 3.0f * sigma ) );
	std::vector<float> kernel( 2 * radius + 1 );
	float sum = 0.0f;
	for( int i = -radius; i <= radius; ++i )
		sum += kernel[i + radius] = std::exp( -0.5f * i * i / ( sigma * sigma ) );
	for( float& weight : kernel )
		weight /= sum;

	const int n = static_cast<int>( size );
	const int mask = n - 1;
	std::vector<float> rows( source.size() ), result( source.size() );
	for( int y = 0; y < n; ++y )
		for( int x = 0; x < n; ++x )
		{
			float value = 0.0f;
			for( int i = -radius; i <= radius; ++i )
				value += kernel[i + radius] * source[y * n + ( ( x + i ) & mask )];
			rows[y * n + x] = value;
		}
	for( int y = 0; y < n; ++y )
		for( int x = 0; x < n; ++x )
		{
			float value = 0.0f;
			for( int i = -radius; i <= radius; ++i )
				value += kernel[i + radius] * rows[( ( y + i ) & mask ) * n + x];
			result[y * n + x] = value;
		}
	return result;
}

// Наклон октавы по U и V (высота в долях повтора: центральная разность × size), среднеквадратичная длина — 1
std::vector<XMFLOAT2> octave( uint32_t size, uint32_t seed, float sigma )
{
	const size_t count = static_cast<size_t>( size ) * size;
	std::vector<float> noise( count );
	std::mt19937 generator( seed );
	std::uniform_real_distribution<float> distribution( -1.0f, 1.0f );
	for( float& value : noise )
		value = distribution( generator );
	const std::vector<float> fine = blur( noise, size, sigma );
	const std::vector<float> coarse = blur( noise, size, 2.0f * sigma );

	const int n = static_cast<int>( size );
	const int mask = n - 1;
	auto height = [&]( int x, int y ) { const size_t i = ( y & mask ) * n + ( x & mask ); return fine[i] - coarse[i]; };
	std::vector<XMFLOAT2> slopes( count );
	double sum = 0.0;
	for( int y = 0; y < n; ++y )
		for( int x = 0; x < n; ++x )
		{
			const float u = ( height( x + 1, y ) - height( x - 1, y ) ) * 0.5f * n;
			const float v = ( height( x, y + 1 ) - height( x, y - 1 ) ) * 0.5f * n;
			slopes[y * n + x] = XMFLOAT2( u, v );
			sum += u * u + v * v;
		}
	const float scale = static_cast<float>( 1.0 / std::sqrt( std::max( sum / count, 1e-12 ) ) );
	for( XMFLOAT2& slope : slopes )
		slope = XMFLOAT2( slope.x * scale, slope.y * scale );
	return slopes;
}

}

namespace WaterRipples
{

bool create( uint32_t size, Texture& texture, ShaderView& view )
{
	if( size < 2 || ( size & ( size - 1 ) ) != 0 )
		return false;
	const std::vector<XMFLOAT2> coarse = octave( size, 7, coarseSigma );
	const std::vector<XMFLOAT2> fine = octave( size, 11, fineSigma );
	std::vector<XMFLOAT4> level( coarse.size() );
	for( size_t i = 0; i < level.size(); ++i )
		level[i] = XMFLOAT4( coarse[i].x, coarse[i].y, fine[i].x, fine[i].y );

	// Мипы — среднее 2 × 2: наклон линеен, среднее наклона — наклон среднего
	ScratchImage image;
	if( FAILED( image.Initialize2D( DXGI_FORMAT_R8G8B8A8_SNORM, size, size, 1, 0 ) ) )
		return false;
	uint32_t width = size;
	for( size_t mip = 0; mip < image.GetMetadata().mipLevels; ++mip )
	{
		if( mip > 0 )
		{
			const uint32_t half = width / 2;
			std::vector<XMFLOAT4> next( static_cast<size_t>( half ) * half );
			for( uint32_t y = 0; y < half; ++y )
				for( uint32_t x = 0; x < half; ++x )
				{
					const XMVECTOR sum = XMLoadFloat4( &level[2 * y * width + 2 * x] ) + XMLoadFloat4( &level[2 * y * width + 2 * x + 1] ) +
										 XMLoadFloat4( &level[( 2 * y + 1 ) * width + 2 * x] ) +
										 XMLoadFloat4( &level[( 2 * y + 1 ) * width + 2 * x + 1] );
					XMStoreFloat4( &next[y * half + x], sum * 0.25f );
				}
			level = std::move( next );
			width = half;
		}
		const Image* pixels = image.GetImage( mip, 0, 0 );
		for( uint32_t y = 0; y < width; ++y )
		{
			PackedVector::XMBYTEN4* row = reinterpret_cast<PackedVector::XMBYTEN4*>( pixels->pixels + y * pixels->rowPitch );
			for( uint32_t x = 0; x < width; ++x )
				PackedVector::XMStoreByteN4( &row[x], XMLoadFloat4( &level[y * width + x] ) / slopeRange );
		}
	}
	return GpuImages::createTexture( image, texture, view );
}

}
