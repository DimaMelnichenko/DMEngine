#include "CustomTexture.h"
#include "Logger\Logger.h"
#include <random>

namespace GS
{

CustomTexture::CustomTexture( uint32_t id, const std::string& name ) : DMTexture( id, name )
{

}

CustomTexture::CustomTexture( CustomTexture&& other ) : DMTexture( std::move(other) )
{
	std::swap( other.m_srv, m_srv );
}

CustomTexture::~CustomTexture()
{
}

const com_unique_ptr<ID3D11ShaderResourceView>& CustomTexture::srv() const
{
	return m_srv;
}

bool CustomTexture::generateTexture()
{

	D3D11_TEXTURE2D_DESC tdesc;
	ZeroMemory( &tdesc, sizeof( D3D11_TEXTURE2D_DESC ) );
	tdesc.Width = m_width;
	tdesc.Height = m_hight;
	tdesc.MipLevels = 1;
	tdesc.ArraySize = 1;
	tdesc.SampleDesc.Count = 1;
	tdesc.SampleDesc.Quality = 0;
	tdesc.Usage = D3D11_USAGE_DEFAULT;
	tdesc.Format = DXGI_FORMAT_R8_SNORM;
	tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	tdesc.CPUAccessFlags = 0;
	tdesc.MiscFlags = 0;
	
	int bpp = 8;
	std::vector<uint8_t> buf;
	buf.resize( m_width * m_hight, 0 );
	// filling the image
	std::mt19937 generator( 1 );
	std::uniform_int_distribution<int> dist( 0, 255 );
	for( uint32_t i = 0; i < ( m_hight * m_width ); i++ )
	{
		buf[i] = dist( generator );
	}

	D3D11_SUBRESOURCE_DATA srInitData;
	ZeroMemory( &srInitData, sizeof( D3D11_SUBRESOURCE_DATA ) );
	srInitData.pSysMem = (void*)&buf[0];
	srInitData.SysMemPitch = m_width;
	srInitData.SysMemSlicePitch = 0;// m_width * m_hight * bpp; // Not needed since this is a 2d texture

	// checking inputs
	if( DMD3D::instance().GetDevice()->CreateTexture2D( &tdesc, &srInitData, nullptr ) == S_FALSE )
	{
		LOG( "Inputs correct" );
	}
	else
	{
		LOG( "wrong inputs" );
	}
	
	// create the texture
	ID3D11Texture2D* texture = nullptr;
	if( FAILED( DMD3D::instance().GetDevice()->CreateTexture2D( &tdesc, &srInitData, &texture ) ) )
	{
		LOG( "Failed create CreateTexture2D" );
		return false;
	}
	com_unique_ptr<ID3D11Texture2D> texture2D;
	texture2D.reset( texture );

	
	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	ZeroMemory( &srDesc, sizeof( D3D11_SHADER_RESOURCE_VIEW_DESC ) );
	srDesc.Format = tdesc.Format;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;

	ID3D11ShaderResourceView* srv = nullptr;
	if( FAILED( DMD3D::instance().GetDevice()->CreateShaderResourceView( texture2D.get(), &srDesc, &srv ) ) )
	{
		LOG( "Can't create Shader Resource View" );
		return false;
	}

	m_srv.reset( srv );

	return true;
}

}