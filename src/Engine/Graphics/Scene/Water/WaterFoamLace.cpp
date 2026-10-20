#include "WaterFoamLace.h"
#include <vector>
#include <DirectXPackedVector.h>
#include <DirectXTex.h>
#include "FoamLace.h"
#include "D3D\TextureImages.h"

using namespace DirectX;

namespace WaterFoamLace
{

namespace
{
constexpr uint32_t seed = 1;
}

bool create( uint32_t size, Texture& texture, ShaderView& view )
{
	if( size < 2 || ( size & ( size - 1 ) ) != 0 )
		return false;
	std::vector<XMFLOAT4> level = GS::generateFoamLace( size, seed );
	ScratchImage image;
	if( FAILED( image.Initialize2D( DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 0 ) ) )
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
			PackedVector::XMUBYTEN4* row = reinterpret_cast<PackedVector::XMUBYTEN4*>( pixels->pixels + y * pixels->rowPitch );
			for( uint32_t x = 0; x < width; ++x )
				PackedVector::XMStoreUByteN4( &row[x], XMLoadFloat4( &level[y * width + x] ) );
		}
	}
	return GpuImages::createTexture( image, texture, view );
}

}
