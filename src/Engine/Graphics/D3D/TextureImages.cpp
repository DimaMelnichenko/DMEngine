#include "TextureImages.h"
#include "DMD3D.h"
#include "Logger\Logger.h"
#include <wincodec.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace GpuImages
{

bool createTexture( const DirectX::ScratchImage& image, Texture& texture, ShaderView& view, TextureViewDesc::Kind viewKind )
{
	// Картинка DirectXTex → описание и подресурсы: срез за срезом, внутри среза мипы — как ждёт DMD3D::createTexture.
	// У объёмной текстуры подресурс — мип со всеми слоями глубины (в ScratchImage они лежат подряд)
	const DirectX::TexMetadata& metadata = image.GetMetadata();
	const bool volume = metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D;
	TextureDesc desc;
	desc.width = static_cast<uint32_t>( metadata.width );
	desc.height = static_cast<uint32_t>( metadata.height );
	desc.depth = volume ? static_cast<uint32_t>( metadata.depth ) : 1;
	desc.arraySize = volume ? 1 : static_cast<uint32_t>( metadata.arraySize );
	desc.mipCount = static_cast<uint32_t>( metadata.mipLevels );
	desc.format = metadata.format;
	desc.usage = TextureUsage::shaderResource;
	desc.cube = metadata.IsCubemap();

	std::vector<TextureData> data;
	data.reserve( static_cast<size_t>( desc.arraySize ) * desc.mipCount );
	for( uint32_t slice = 0; slice < desc.arraySize; ++slice )
	{
		for( uint32_t mip = 0; mip < desc.mipCount; ++mip )
		{
			const DirectX::Image* source = image.GetImage( mip, volume ? 0 : slice, 0 );
			if( !source )
				return false;
			TextureData subresource;
			subresource.data = source->pixels;
			subresource.rowPitch = static_cast<uint32_t>( source->rowPitch );
			subresource.slicePitch = static_cast<uint32_t>( source->slicePitch );
			data.push_back( subresource );
		}
	}

	if( !DMD3D::instance().createTexture( desc, data.data(), texture ) )
		return false;
	TextureViewDesc viewDesc;
	viewDesc.kind = viewKind;
	return DMD3D::instance().createShaderView( texture, viewDesc, view );
}

bool captureTexture( const Texture& texture, DirectX::ScratchImage& image )
{
	// Все подресурсы через readback-буфер (DMD3D::captureTexture ждёт GPU) → ScratchImage той же раскладки
	const TextureDesc& desc = texture.desc();
	DirectX::TexMetadata metadata = {};
	metadata.width = desc.width;
	metadata.height = desc.height;
	metadata.depth = desc.depth;
	metadata.arraySize = desc.depth > 1 ? 1 : desc.arraySize;
	metadata.mipLevels = desc.mipCount;
	metadata.format = desc.format;
	metadata.dimension = desc.depth > 1 ? DirectX::TEX_DIMENSION_TEXTURE3D : DirectX::TEX_DIMENSION_TEXTURE2D;
	metadata.miscFlags = desc.cube ? DirectX::TEX_MISC_TEXTURECUBE : 0;
	if( FAILED( image.Initialize( metadata ) ) )
		return false;

	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( texture, copies, bytes ) )
		return false;

	const bool volume = desc.depth > 1;
	for( uint32_t slice = 0; slice < metadata.arraySize; ++slice )
	{
		for( uint32_t mip = 0; mip < desc.mipCount; ++mip )
		{
			const DMD3D::SubresourceCopy& copy = copies[static_cast<size_t>( slice ) * desc.mipCount + mip];
			const uint32_t depth = volume ? std::max( desc.depth >> mip, 1u ) : 1;
			for( uint32_t z = 0; z < depth; ++z )
			{
				const DirectX::Image* target = image.GetImage( mip, volume ? 0 : slice, z );
				if( !target )
					return false;
				for( uint32_t row = 0; row < copy.rows; ++row )
				{
					const uint8_t* source = bytes.data() + copy.offset + ( static_cast<size_t>( z ) * copy.rows + row ) * copy.rowPitch;
					std::memcpy( target->pixels + static_cast<size_t>( row ) * target->rowPitch, source,
								 std::min<size_t>( copy.rowBytes, target->rowPitch ) );
				}
			}
		}
	}
	return true;
}

bool saveFloatDDS( const std::wstring& path, uint32_t width, uint32_t height, const float* values )
{
	DirectX::Image image = {};
	image.width = width;
	image.height = height;
	image.format = DXGI_FORMAT_R32_FLOAT;
	image.rowPitch = static_cast<size_t>( width ) * sizeof( float );
	image.slicePitch = image.rowPitch * height;
	image.pixels = reinterpret_cast<uint8_t*>( const_cast<float*>( values ) );
	return SUCCEEDED( DirectX::SaveToDDSFile( image, DirectX::DDS_FLAGS_FORCE_DX10_EXT, path.c_str() ) );
}

bool saveImage( const std::wstring& path, uint32_t width, uint32_t height, DXGI_FORMAT format, const uint8_t* bytes, uint32_t rowPitch )
{
	DirectX::Image image = {};
	image.width = width;
	image.height = height;
	image.format = format;
	image.rowPitch = rowPitch;
	image.slicePitch = static_cast<size_t>( rowPitch ) * height;
	image.pixels = const_cast<uint8_t*>( bytes );
	const bool jpeg = path.size() > 4 && ( _wcsicmp( path.c_str() + path.size() - 4, L".jpg" ) == 0 ||
										   _wcsicmp( path.c_str() + path.size() - 5, L".jpeg" ) == 0 );
	// Без альфы: у заднего буфера она не имеет смысла, а PNG с ней получился бы полупрозрачным
	return SUCCEEDED( DirectX::SaveToWICFile( image, DirectX::WIC_FLAGS_IGNORE_SRGB | DirectX::WIC_FLAGS_FORCE_RGB,
											  DirectX::GetWICCodec( jpeg ? DirectX::WIC_CODEC_JPEG : DirectX::WIC_CODEC_PNG ), path.c_str(),
											  jpeg ? &GUID_WICPixelFormat24bppBGR : &GUID_WICPixelFormat24bppBGR ) );
}

}
