#include "CubeTarget.h"
#include "DMD3D.h"

bool CubeTarget::create( uint32_t size, uint32_t mipCount, DXGI_FORMAT format, bool generateMips )
{
	ID3D11Device* device = DMD3D::instance().GetDevice();

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = size;
	desc.Height = size;
	desc.MipLevels = mipCount;
	desc.ArraySize = 6;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | ( generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0 );

	ID3D11Texture2D* texture = nullptr;
	if( FAILED( device->CreateTexture2D( &desc, nullptr, &texture ) ) )
		return false;
	m_texture = make_com_ptr<ID3D11Texture2D>( texture );
	texture->GetDesc( &desc );

	m_targets.clear();
	for( uint32_t mip = 0; mip < desc.MipLevels; ++mip )
	{
		for( uint32_t face = 0; face < 6; ++face )
		{
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rtvDesc.Texture2DArray.MipSlice = mip;
			rtvDesc.Texture2DArray.FirstArraySlice = face;
			rtvDesc.Texture2DArray.ArraySize = 1;
			ID3D11RenderTargetView* rtv = nullptr;
			if( FAILED( device->CreateRenderTargetView( texture, &rtvDesc, &rtv ) ) )
				return false;
			m_targets.push_back( make_com_ptr<ID3D11RenderTargetView>( rtv ) );
		}
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = desc.MipLevels;
	ID3D11ShaderResourceView* srv = nullptr;
	if( FAILED( device->CreateShaderResourceView( texture, &srvDesc, &srv ) ) )
		return false;
	m_srv = make_com_ptr<ID3D11ShaderResourceView>( srv );

	m_format = format;
	m_size = size;
	m_mipCount = desc.MipLevels;
	return true;
}

bool CubeTarget::createFacesView( uint32_t mip )
{
	D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = m_format;
	desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	desc.Texture2DArray.MostDetailedMip = mip;
	desc.Texture2DArray.MipLevels = 1;
	desc.Texture2DArray.FirstArraySlice = 0;
	desc.Texture2DArray.ArraySize = 6;
	ID3D11ShaderResourceView* srv = nullptr;
	if( FAILED( DMD3D::instance().GetDevice()->CreateShaderResourceView( m_texture.get(), &desc, &srv ) ) )
		return false;
	m_facesSRV = make_com_ptr<ID3D11ShaderResourceView>( srv );
	return true;
}
