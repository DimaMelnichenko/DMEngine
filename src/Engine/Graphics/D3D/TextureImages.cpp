#include "TextureImages.h"
#include "DMD3D.h"

namespace GpuImages
{

bool createTexture( const DirectX::ScratchImage& image, Texture& texture, ShaderView& view, TextureViewDesc::Kind viewKind )
{
	const DirectX::TexMetadata& metadata = image.GetMetadata();
	ID3D11Resource* resource = nullptr;
	if( FAILED( DirectX::CreateTexture( DMD3D::instance().GetDevice(), image.GetImages(), image.GetImageCount(), metadata, &resource ) ) )
		return false;

	TextureDesc desc;
	desc.width = static_cast<uint32_t>( metadata.width );
	desc.height = static_cast<uint32_t>( metadata.height );
	desc.depth = metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D ? static_cast<uint32_t>( metadata.depth ) : 1;
	desc.arraySize = metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D ? 1 : static_cast<uint32_t>( metadata.arraySize );
	desc.mipCount = static_cast<uint32_t>( metadata.mipLevels );
	desc.format = metadata.format;
	desc.usage = TextureUsage::shaderResource;
	desc.cube = metadata.IsCubemap();
	texture.reset( resource, desc );

	TextureViewDesc viewDesc;
	viewDesc.kind = viewKind;
	return DMD3D::instance().createShaderView( texture, viewDesc, view );
}

bool captureTexture( const Texture& texture, DirectX::ScratchImage& image )
{
	return SUCCEEDED( DirectX::CaptureTexture( DMD3D::instance().GetDevice(), DMD3D::instance().GetDeviceContext(), texture.handle(), image ) );
}

}
