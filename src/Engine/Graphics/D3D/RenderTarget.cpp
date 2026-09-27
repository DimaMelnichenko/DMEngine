#include "RenderTarget.h"
#include "DMD3D.h"

bool RenderTarget::create( uint32_t width, uint32_t height, DXGI_FORMAT format )
{
	ID3D11Device* device = DMD3D::instance().GetDevice();

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture2D* texture = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	if( FAILED( device->CreateTexture2D( &desc, nullptr, &texture ) ) )
		return false;
	m_texture = make_com_ptr<ID3D11Texture2D>( texture );
	if( FAILED( device->CreateRenderTargetView( texture, nullptr, &rtv ) ) )
		return false;
	m_rtv = make_com_ptr<ID3D11RenderTargetView>( rtv );
	if( FAILED( device->CreateShaderResourceView( texture, nullptr, &srv ) ) )
		return false;
	m_srv = make_com_ptr<ID3D11ShaderResourceView>( srv );

	m_width = width;
	m_height = height;
	return true;
}
