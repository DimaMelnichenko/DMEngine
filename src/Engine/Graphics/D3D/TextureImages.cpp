#include "TextureImages.h"
#include "DMD3D.h"
#include "Logger\Logger.h"

namespace GpuImages
{

namespace
{

void notImplemented( const char* what )
{
	static bool logged[2] = {};
	const int index = what[1] == 'r' ? 0 : 1;	// createTexture / captureTexture
	if( !logged[index] )
	{
		LOG( std::string( "GpuImages::" ) + what + " is not implemented yet (D3D12 port, veha M2)" );
		logged[index] = true;
	}
}

}

bool createTexture( const DirectX::ScratchImage& image, Texture& texture, ShaderView& view, TextureViewDesc::Kind )
{
	// Веха M2: DirectXTex D3D12 — CreateTexture + PrepareUpload, копирование через staging-буфер. Пока — текстура без
	// ресурса с описанием картинки, чтобы загрузка уровня шла дальше (как остальные заглушки DMD3D)
	notImplemented( "createTexture" );
	const DirectX::TexMetadata& metadata = image.GetMetadata();
	TextureDesc desc;
	desc.width = static_cast<uint32_t>( metadata.width );
	desc.height = static_cast<uint32_t>( metadata.height );
	desc.depth = metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D ? static_cast<uint32_t>( metadata.depth ) : 1;
	desc.arraySize = metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D ? 1 : static_cast<uint32_t>( metadata.arraySize );
	desc.mipCount = static_cast<uint32_t>( metadata.mipLevels );
	desc.format = metadata.format;
	desc.cube = metadata.IsCubemap();
	texture.reset( nullptr, nullptr, desc );
	view.reset();
	return true;
}

bool captureTexture( const Texture&, DirectX::ScratchImage& )
{
	// Веха M2: CaptureTexture( ID3D12CommandQueue*, … )
	notImplemented( "captureTexture" );
	return false;
}

}
