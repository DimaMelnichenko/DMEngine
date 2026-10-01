#include "ImageMips.h"
#include <algorithm>
#include <cstdint>
#include <vector>

using namespace DirectX;

namespace
{

// Мипы с сохранением покрытия альфы (Castaño 2010, как Mip Maps Preserve Coverage в Unity): альфа мипа умножается
// на такое число, чтобы доля текселей выше порога совпала с нулевым мипом. Число находится по гистограмме альфы мипа
// (8 бит — 256 столбцов) за один проход, а не подбором с субсэмплингом, как ScaleMipMapsAlphaForCoverage в DirectXTex
// (у той — секунды на 2048²). Форматы — RGBA / BGRA по 8 бит, альфа — четвёртый байт
bool preserveAlphaCoverage( const ScratchImage& image, float cutoff )
{
	const TexMetadata& metadata = image.GetMetadata();
	const DXGI_FORMAT format = MakeLinear( metadata.format );
	if( format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_B8G8R8A8_UNORM )
		return false;

	auto histogram = []( const Image& mip, uint64_t( &counts )[256] )
	{
		std::fill( std::begin( counts ), std::end( counts ), 0 );
		for( size_t y = 0; y < mip.height; ++y )
		{
			const uint8_t* row = mip.pixels + y * mip.rowPitch;
			for( size_t x = 0; x < mip.width; ++x )
				++counts[row[x * 4 + 3]];
		}
	};

	const float threshold = cutoff * 255.0f;	// тексель виден, если альфа (в байтах) больше
	for( size_t item = 0; item < metadata.arraySize; ++item )
	{
		uint64_t counts[256];
		const Image& base = *image.GetImage( 0, item, 0 );
		histogram( base, counts );
		uint64_t covered = 0;
		for( int value = 0; value < 256; ++value )
			covered += value > threshold ? counts[value] : 0;
		const double coverage = double( covered ) / double( base.width * base.height );
		if( covered == 0 )
			continue;

		for( size_t level = 1; level < metadata.mipLevels; ++level )
		{
			const Image& mip = *image.GetImage( level, item, 0 );
			histogram( mip, counts );
			// Наименьшая альфа b, начиная с которой тексели мипа должны остаться видимыми
			const double target = coverage * double( mip.width * mip.height );
			uint64_t above = 0;
			int b = 256;
			while( b > 1 && double( above ) < target - 0.5 )
				above += counts[--b];
			if( b > 255 )
				continue;
			// b проходит порог, b − 1 — нет
			const float scale = threshold / ( float( b ) - 0.5f );
			for( size_t y = 0; y < mip.height; ++y )
			{
				uint8_t* row = mip.pixels + y * mip.rowPitch;
				for( size_t x = 0; x < mip.width; ++x )
					row[x * 4 + 3] = static_cast<uint8_t>( std::min( 255.0f, row[x * 4 + 3] * scale + 0.5f ) );
			}
		}
	}
	return true;
}


}

namespace ImageMips
{

bool generate( ScratchImage& image, float preserveAlphaCoverageCutoff )
{
	ScratchImage mipmapImage;
	if( FAILED( GenerateMipMaps( image.GetImages(), image.GetImageCount(), image.GetMetadata(), TEX_FILTER_DEFAULT, 0, mipmapImage ) ) )
		return false;
	std::swap( mipmapImage, image );
	return preserveAlphaCoverageCutoff <= 0.0f || preserveAlphaCoverage( image, preserveAlphaCoverageCutoff );
}

bool dilateTransparent( ScratchImage& image, int passes, const ScratchImage* coverage )
{
	const TexMetadata& metadata = image.GetMetadata();
	const DXGI_FORMAT format = MakeLinear( metadata.format );
	const bool rgba8 = format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM;
	const bool r16 = format == DXGI_FORMAT_R16_UNORM || format == DXGI_FORMAT_R16G16_UNORM;
	if( !rgba8 && !( r16 && coverage ) )
		return false;
	if( coverage )
	{
		const TexMetadata& mask = coverage->GetMetadata();
		const DXGI_FORMAT maskFormat = MakeLinear( mask.format );
		if( ( maskFormat != DXGI_FORMAT_R8G8B8A8_UNORM && maskFormat != DXGI_FORMAT_B8G8R8A8_UNORM ) ||
			mask.width != metadata.width || mask.height != metadata.height || mask.arraySize < metadata.arraySize )
			return false;
	}
	// Тексель: RGBA по 8 бит — растекаются три канала, R16 и R16G16 — все
	const int channels = rgba8 ? 3 : format == DXGI_FORMAT_R16G16_UNORM ? 2 : 1;
	const size_t texelSize = rgba8 ? 4 : channels * 2;
	const auto read = [rgba8]( const uint8_t* texel, int c ) -> uint32_t
	{
		return rgba8 ? texel[c] : reinterpret_cast<const uint16_t*>( texel )[c];
	};
	const auto write = [rgba8]( uint8_t* texel, int c, uint32_t value )
	{
		if( rgba8 )
			texel[c] = static_cast<uint8_t>( value );
		else
			reinterpret_cast<uint16_t*>( texel )[c] = static_cast<uint16_t>( value );
	};

	for( size_t item = 0; item < metadata.arraySize; ++item )
	{
		const Image& slice = *image.GetImage( 0, item, 0 );
		const Image& mask = coverage ? *coverage->GetImage( 0, item, 0 ) : slice;
		const size_t width = slice.width;
		const size_t height = slice.height;
		// Заполненные тексели: сначала непрозрачные, за каждый проход — ещё полоса в тексель
		std::vector<uint8_t> filled( width * height );
		for( size_t y = 0; y < height; ++y )
			for( size_t x = 0; x < width; ++x )
				filled[y * width + x] = mask.pixels[y * mask.rowPitch + x * 4 + 3] > 0 ? 1 : 0;
		for( int pass = 0; pass < passes; ++pass )
		{
			std::vector<uint8_t> next = filled;
			for( size_t y = 0; y < height; ++y )
			{
				for( size_t x = 0; x < width; ++x )
				{
					if( filled[y * width + x] )
						continue;
					uint32_t sum[3] = {};
					uint32_t count = 0;
					for( int dy = -1; dy <= 1; ++dy )
					for( int dx = -1; dx <= 1; ++dx )
					{
						const int nx = static_cast<int>( x ) + dx;
						const int ny = static_cast<int>( y ) + dy;
						if( nx < 0 || ny < 0 || nx >= static_cast<int>( width ) || ny >= static_cast<int>( height ) || !filled[ny * width + nx] )
							continue;
						const uint8_t* texel = slice.pixels + ny * slice.rowPitch + nx * texelSize;
						for( int c = 0; c < channels; ++c )
							sum[c] += read( texel, c );
						++count;
					}
					if( count == 0 )
						continue;
					uint8_t* texel = slice.pixels + y * slice.rowPitch + x * texelSize;
					for( int c = 0; c < channels; ++c )
						write( texel, c, ( sum[c] + count / 2 ) / count );
					next[y * width + x] = 1;
				}
			}
			filled.swap( next );
		}
	}
	return true;
}

}
