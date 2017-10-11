#include "DMRenderFilter.h"



DMRenderFilter::DMRenderFilter(  ) : m_pass( 0 )
{

}

DMRenderFilter::~DMRenderFilter()
{

}

void DMRenderFilter::Initialize( DMFullScreen* full_screen, DMFullScreenShader* shader )
{
	m_full_screen = full_screen;
	m_shader = shader;
}

void DMRenderFilter::selectPass( unsigned int pass )
{
	m_pass = pass;
}

void DMRenderFilter::setSRV( unsigned int index, ID3D11ShaderResourceView* srv )
{
	if( index > m_srv.size() - 1 || !m_srv.size() )
	{
		while( index > m_srv.size() )
		{
			m_srv.push_back( nullptr );
		}

		m_srv.push_back( srv );
	}
	else
	{
		m_srv[index] = srv;
	}
}

void DMRenderFilter::clearSRV()
{
	// clear all slots
	static ID3D11ShaderResourceView* clear_srv[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 15, clear_srv );
	m_srv.clear();
}

void DMRenderFilter::Render( const DMCamera& camera, DMRenderTexture* render_target_texture )
{
	DMD3D::instance().TurnZBufferOff();

	if( render_target_texture )
	{
		render_target_texture->SetRenderTarget();
		render_target_texture->ClearRenderTarget( 0.0f, 0.0f, 0.0f, 1.0f );
	}

	/////////////////////////////////////////////////////////////
	//////////	full screen quad	/////////////////////////////

	m_full_screen->Render();
	m_shader->Prepare( camera, m_pass );
	
	if( m_srv.size() )
	{
		DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, m_srv.size(), &m_srv[0] );
	}
	m_shader->Render( 3, nullptr );
	
	/////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////

	DMD3D::instance().TurnZBufferOn();

	if( render_target_texture )
	{
		DMD3D::instance().SetBackBufferRenderTarget();
		DMD3D::instance().ResetViewport();
	}


	clearSRV();
}