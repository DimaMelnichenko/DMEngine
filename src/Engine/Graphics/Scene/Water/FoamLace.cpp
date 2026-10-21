#include "FoamLace.h"
#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>

using namespace DirectX;

namespace GS
{

namespace
{

constexpr float normalBump = 2.0f;		// крутизна нормали: высота маски на шаг текселя
constexpr float normalBlur = 1.0f;		// размытие маски для нормали, текселей

// PCG (Jarzynski, Olano 2020)
uint32_t hash( uint32_t value )
{
	const uint32_t state = value * 747796405u + 2891336453u;
	const uint32_t word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

// Случайное 0…1 узла решётки (i, j) по зерну
float lattice( int i, int j, uint32_t seed )
{
	return ( hash( static_cast<uint32_t>( i ) + hash( static_cast<uint32_t>( j ) + hash( seed ) ) ) >> 8 ) / 16777216.0f;
}

float smoothstep( float from, float to, float x )
{
	const float t = std::clamp( ( x - from ) / ( to - from ), 0.0f, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

int wrap( int i, int cells )
{
	return ( i % cells + cells ) % cells;
}

// Плавный шум 0…1 в точке (u, v) карты (0…1): решётка cells × cells по повтору, smoothstep между узлами — бесшовно
float valueNoise( float u, float v, int cells, uint32_t seed )
{
	const float x = u * cells - 0.5f;
	const float y = v * cells - 0.5f;
	const int i = static_cast<int>( std::floor( x ) );
	const int j = static_cast<int>( std::floor( y ) );
	const float fx = x - i;
	const float fy = y - j;
	const float sx = fx * fx * ( 3.0f - 2.0f * fx );
	const float sy = fy * fy * ( 3.0f - 2.0f * fy );
	const float a = lattice( wrap( i, cells ), wrap( j, cells ), seed );
	const float b = lattice( wrap( i + 1, cells ), wrap( j, cells ), seed );
	const float c = lattice( wrap( i, cells ), wrap( j + 1, cells ), seed );
	const float d = lattice( wrap( i + 1, cells ), wrap( j + 1, cells ), seed );
	return ( a * ( 1.0f - sx ) + b * sx ) * ( 1.0f - sy ) + ( c * ( 1.0f - sx ) + d * sx ) * sy;
}

struct Cells
{
	float first;	// до ближайшей точки, ячеек
	float second;	// до второй
};

// Шум Ворлея: точка на ячейку сетки cells × cells (случайно в ячейке), сетка искривлена плавным шумом на warp ячеек — стенки
// не прямые; бесшовно (ячейки по модулю)
Cells worley( float u, float v, int cells, uint32_t seed, float warp )
{
	const float wx = 2.0f * ( valueNoise( u, v, 6, seed + 1 ) - 0.5f ) + ( valueNoise( u, v, 13, seed + 2 ) - 0.5f );
	const float wy = 2.0f * ( valueNoise( u, v, 6, seed + 3 ) - 0.5f ) + ( valueNoise( u, v, 13, seed + 4 ) - 0.5f );
	const float x = u * cells + warp * wx;
	const float y = v * cells + warp * wy;
	const int ci = static_cast<int>( std::floor( x ) );
	const int cj = static_cast<int>( std::floor( y ) );
	Cells result{ 1e9f, 1e9f };
	for( int dj = -1; dj <= 1; ++dj )
		for( int di = -1; di <= 1; ++di )
		{
			const int ni = ci + di;
			const int nj = cj + dj;
			const int wi = wrap( ni, cells );
			const int wj = wrap( nj, cells );
			const float px = ni + lattice( wi, wj, seed + 5 );
			const float py = nj + lattice( wi, wj, seed + 6 );
			const float distance = std::hypot( x - px, y - py );
			if( distance < result.first )
			{
				result.second = result.first;
				result.first = distance;
			}
			else
				result.second = std::min( result.second, distance );
		}
	return result;
}

// Значения → ранги 0…1 (равномерно): порог по доле пены линейно сдвигает долю площади
void equalize( std::vector<float>& values )
{
	std::vector<uint32_t> order( values.size() );
	std::iota( order.begin(), order.end(), 0u );
	std::stable_sort( order.begin(), order.end(), [&values]( uint32_t a, uint32_t b ) { return values[a] < values[b]; } );
	const float count = static_cast<float>( values.size() );
	for( size_t rank = 0; rank < order.size(); ++rank )
		values[order[rank]] = ( rank + 0.5f ) / count;
}

// Гауссово размытие с повтором через край
std::vector<float> blur( const std::vector<float>& source, uint32_t size, float sigma )
{
	const int radius = static_cast<int>( std::ceil( 3.0f * sigma ) );
	std::vector<float> kernel( 2 * radius + 1 );
	float sum = 0.0f;
	for( int k = -radius; k <= radius; ++k )
		sum += kernel[k + radius] = std::exp( -0.5f * k * k / ( sigma * sigma ) );
	for( float& weight : kernel )
		weight /= sum;
	const int n = static_cast<int>( size );
	std::vector<float> rows( source.size() ), result( source.size() );
	for( int y = 0; y < n; ++y )
		for( int x = 0; x < n; ++x )
		{
			float value = 0.0f;
			for( int k = -radius; k <= radius; ++k )
				value += kernel[k + radius] * source[static_cast<size_t>( y ) * n + wrap( x + k, n )];
			rows[static_cast<size_t>( y ) * n + x] = value;
		}
	for( int y = 0; y < n; ++y )
		for( int x = 0; x < n; ++x )
		{
			float value = 0.0f;
			for( int k = -radius; k <= radius; ++k )
				value += kernel[k + radius] * rows[static_cast<size_t>( wrap( y + k, n ) ) * n + x];
			result[static_cast<size_t>( y ) * n + x] = value;
		}
	return result;
}

// Маска и шум растворения → каналы узора: нормаль — по размытой маске, как у pack_water_foam.py
std::vector<XMFLOAT4> packFoam( const std::vector<float>& mask, const std::vector<float>& dissolve, uint32_t size )
{
	const std::vector<float> height = blur( mask, size, normalBlur );
	const int n = static_cast<int>( size );
	std::vector<XMFLOAT4> texels( mask.size() );
	for( int y = 0; y < n; ++y )
		for( int x = 0; x < n; ++x )
		{
			const size_t index = static_cast<size_t>( y ) * n + x;
			const float dx = 0.5f * normalBump * ( height[static_cast<size_t>( y ) * n + wrap( x + 1, n )] - height[static_cast<size_t>( y ) * n + wrap( x - 1, n )] );
			const float dy = 0.5f * normalBump * ( height[static_cast<size_t>( wrap( y + 1, n ) ) * n + x] - height[static_cast<size_t>( wrap( y - 1, n ) ) * n + x] );
			XMFLOAT3 normal;
			XMStoreFloat3( &normal, XMVector3Normalize( XMVectorSet( -dx, -dy, 1.0f, 0.0f ) ) );
			texels[index] = XMFLOAT4( mask[index], 0.5f + 0.5f * normal.x, 0.5f + 0.5f * normal.y, dissolve[index] );
		}
	return texels;
}

}

std::vector<XMFLOAT4> generateFoamLace( uint32_t size, uint32_t seed )
{
	const size_t count = static_cast<size_t>( size ) * size;
	std::vector<float> mask( count ), dissolve( count );
	std::vector<uint32_t> rows( size );
	std::iota( rows.begin(), rows.end(), 0u );
	std::for_each( std::execution::par, rows.begin(), rows.end(),
				   [&]( uint32_t y )
				   {
					   for( uint32_t x = 0; x < size; ++x )
					   {
						   const float u = ( x + 0.5f ) / size;
						   const float v = ( y + 0.5f ) / size;
						   // Плотность пены — плавные пятна: где её много, кружево гуще и между стенками — мелкая пена
						   const float density = smoothstep( 0.2f, 0.8f, 0.65f * valueNoise( u, v, 4, seed + 10 ) + 0.35f * valueNoise( u, v, 8, seed + 11 ) );
						   // Крупное кружево: стенки разной толщины, с разрывами
						   const Cells lace = worley( u, v, 9, seed + 20, 0.35f );
						   const float thickness = 0.05f + 0.22f * std::pow( valueNoise( u, v, 18, seed + 12 ), 2.0f );
						   const float gaps = smoothstep( 0.25f, 0.45f, valueNoise( u, v, 14, seed + 13 ) );
						   const float walls = ( 1.0f - smoothstep( 0.0f, thickness, lace.second - lace.first ) ) * ( 0.35f + 0.65f * gaps );
						   // Сеть помельче — кусками
						   const Cells net = worley( u, v, 24, seed + 30, 0.5f );
						   const float netWalls = ( 1.0f - smoothstep( 0.0f, 0.2f, net.second - net.first ) ) *
												  smoothstep( 0.3f, 0.6f, valueNoise( u, v, 20, seed + 14 ) );
						   // Мелкая пена пузырьков
						   const Cells froth = worley( u, v, 56, seed + 40, 0.6f );
						   const float frothWalls = 1.0f - smoothstep( 0.0f, 0.3f, froth.second - froth.first );
						   const size_t index = static_cast<size_t>( y ) * size + x;
						   mask[index] = walls * ( 0.5f + 0.5f * density ) + 0.45f * netWalls * density +
										 0.55f * frothWalls * std::pow( density, 1.5f ) + 0.1f * density;
						   dissolve[index] = 0.7f * valueNoise( u, v, 6, seed + 50 ) + 0.3f * valueNoise( u, v, 18, seed + 51 );
					   }
				   } );
	equalize( mask );
	equalize( dissolve );
	return packFoam( mask, dissolve, size );
}

std::vector<XMFLOAT4> generateFoamDense( uint32_t size, uint32_t seed )
{
	const size_t count = static_cast<size_t>( size ) * size;
	std::vector<float> mask( count ), dissolve( count );
	std::vector<uint32_t> rows( size );
	std::iota( rows.begin(), rows.end(), 0u );
	std::for_each( std::execution::par, rows.begin(), rows.end(),
				   [&]( uint32_t y )
				   {
					   for( uint32_t x = 0; x < size; ++x )
					   {
						   const float u = ( x + 0.5f ) / size;
						   const float v = ( y + 0.5f ) / size;
						   // Пузыри трёх размеров вплотную (стенки толще, чем у кружева) и плавные пятна плотнее и реже
						   const Cells large = worley( u, v, 20, seed + 20, 0.5f );
						   const Cells middle = worley( u, v, 44, seed + 30, 0.6f );
						   const Cells small = worley( u, v, 90, seed + 40, 0.6f );
						   const float patches = smoothstep( 0.25f, 0.75f, 0.6f * valueNoise( u, v, 5, seed + 10 ) + 0.4f * valueNoise( u, v, 11, seed + 11 ) );
						   const size_t index = static_cast<size_t>( y ) * size + x;
						   mask[index] = 0.35f * ( 1.0f - smoothstep( 0.0f, 0.35f, large.second - large.first ) ) +
										 0.35f * ( 1.0f - smoothstep( 0.0f, 0.4f, middle.second - middle.first ) ) +
										 0.3f * ( 1.0f - smoothstep( 0.0f, 0.45f, small.second - small.first ) ) + 0.25f * patches;
						   dissolve[index] = 0.7f * valueNoise( u, v, 6, seed + 50 ) + 0.3f * valueNoise( u, v, 18, seed + 51 );
					   }
				   } );
	equalize( mask );
	equalize( dissolve );
	for( float& value : mask )
		value = denseFoamGap + ( 1.0f - denseFoamGap ) * value;
	return packFoam( mask, dissolve, size );
}

}
