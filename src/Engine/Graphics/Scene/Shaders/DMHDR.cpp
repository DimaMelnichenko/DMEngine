#include "DMHDR.h"



DMHDR::DMHDR()
{
}


DMHDR::~DMHDR()
{
}

void DMHDR::Initialize( float width, float height, float brigthDivider, float blurDivider )
{
	m_main_rt.Initialize( DXGI_FORMAT_R16G16B16A16_UNORM, width, height );

	m_bright_rt.Initialize( DXGI_FORMAT_R16G16B16A16_UNORM, width / brigthDivider, height / brigthDivider, false );

	m_blur_rt.Initialize( DXGI_FORMAT_R16G16B16A16_UNORM, width / blurDivider, height / blurDivider, false );

	m_downsample_rt.Initialize( DXGI_FORMAT_R16G16B16A16_UNORM, width / blurDivider, height / blurDivider, false );

	m_blur_camera.Initialize( DMCamera::CT_ORTHO, width / blurDivider, height / blurDivider, 0.1, 2.0 );
	
	m_bright_camera.Initialize( DMCamera::CT_ORTHO, width / brigthDivider, height / brigthDivider, 0.1, 2.0 );

	/*float down_resolution = 243.0f;

	while( down_resolution > 1.0f )
	{
		std::unique_ptr<DMRenderTexture> rt( new DMRenderTexture( m_dmd3d ) );
		rt->Initialize( DXGI_FORMAT_R32G32B32A32_FLOAT, down_resolution, down_resolution, false );
		m_luminance_rt.push_back( std::move( rt ) );
		down_resolution /= 3.0f;
	}
	*/
	PSBuffer buffer;
	buffer.mainTexelSize = 1.0f / width;
	buffer.brightTexelSize = 1.0f / width / brigthDivider;
	buffer.blurTexelSize = 1.0f / width / blurDivider;

	D3D11_SUBRESOURCE_DATA srData;
	srData.pSysMem = &buffer;
	srData.SysMemPitch = sizeof( PSBuffer );
	srData.SysMemSlicePitch = 0;

	DMD3D::instance().createShaderConstantBuffer( sizeof( PSBuffer ), m_psBuffer, &srData );
		
}

void DMHDR::begin()
{
	m_main_rt.SetRenderTarget();
	m_main_rt.ClearRenderTarget( 0.0, 0.0, 0.0, 1.0f );
}

void DMHDR::postprocess_and_end( const DMCamera& camera, DMRenderFilter* filter )
{
	ID3D11Buffer* buffer = m_psBuffer.get();
	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 1, 1, &buffer );

	DMD3D::instance().TurnZBufferOff();
	
	// bright pass
	m_bright_rt.SetRenderTarget();
	m_bright_rt.ClearRenderTarget( 0.0, 0.0, 0.0, 1.0f );
	filter->setSRV( 0, m_main_rt.GetShaderResourceView() );
	filter->selectPass( 2 );
	filter->Render( m_bright_camera, nullptr );

	//m_debug_texture = m_bright_rt.GetShaderResourceView();
	
	// downsample
	m_downsample_rt.SetRenderTarget();
	m_downsample_rt.ClearRenderTarget( 0.0, 0.0, 0.0, 1.0f );
	filter->setSRV( 0, m_bright_rt.GetShaderResourceView() );
	filter->selectPass( 3 );
	filter->Render( m_blur_camera, nullptr );

	//m_debug_texture = m_downsample_rt.GetShaderResourceView();
	
	// horizontal blur
	m_blur_rt.SetRenderTarget();
	m_blur_rt.ClearRenderTarget( 0.0, 0.0, 0.0, 1.0f );
	filter->setSRV( 0, m_downsample_rt.GetShaderResourceView() );
	filter->selectPass( 4 );
	filter->Render( m_blur_camera, nullptr );

	//m_debug_texture = m_blur_rt.GetShaderResourceView();
	
	// vertical blur
	m_bright_rt.SetRenderTarget();
	m_bright_rt.ClearRenderTarget( 0.0, 0.0, 0.0, 1.0f );
	filter->setSRV( 0, m_blur_rt.GetShaderResourceView() );
	filter->selectPass( 5 );
	filter->Render( m_blur_camera, nullptr );

	m_debug_texture = m_bright_rt.GetShaderResourceView();
	
	DMD3D::instance().TurnZBufferOn();
	DMD3D::instance().SetBackBufferRenderTarget();
	DMD3D::instance().ResetViewport();
}

ID3D11ShaderResourceView* DMHDR::mainTexture()
{
	return m_main_rt.GetShaderResourceView();
}

ID3D11ShaderResourceView* DMHDR::brightTexture()
{
	return m_bright_rt.GetShaderResourceView();
}

ID3D11ShaderResourceView* DMHDR::debugTexture()
{
	return m_debug_texture;
}