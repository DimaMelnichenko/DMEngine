#pragma once

#include "DirectX.h"

#include "Common\DDS.h"
#include "Utils\utilites.h"
#include <string>
#include <memory>


class DMHeightMap
{

public:
	DMHeightMap();
	~DMHeightMap();

	float height( float x, float y );

	HRESULT LoadRaw( const std::string& file_name, unsigned int width, unsigned int height, unsigned char bits );
	HRESULT LoadMap( const std::string& szFileName );
	D3DXVECTOR2 size();
	ID3D11ShaderResourceView* map();

private:
	std::unique_ptr<unsigned char> m_data;
	DXGI_FORMAT m_fmt;
	unsigned int m_RowBytes;
	DDS_HEADER m_pHeader;
	unsigned int m_pBitSize;
	std::unique_ptr<unsigned char> m_pHeapData;
	com_unique_ptr<ID3D11Texture2D> m_heightmap_texture;
	com_unique_ptr<ID3D11ShaderResourceView> m_heightmap_srv;

private:
	HRESULT LoadStandartFile( const std::string& szFileName );
	HRESULT CreateDDSTextureFromFile( const std::string& szFileName );
	void GetSurfaceInfo( UINT width, UINT height, DXGI_FORMAT fmt );
	UINT BitsPerPixel( DXGI_FORMAT fmt );
	HRESULT LoadTextureDataFromFile( const std::string& szFileName );
	DXGI_FORMAT GetDXGIFormat( const DDS_PIXELFORMAT& ddpf );
	float pixel( float, float );
	bool in_triangle( D3DXVECTOR2* d, D3DXVECTOR2* abc );
	bool checkHeightOfTriangle( D3DXVECTOR3* point, D3DXVECTOR3* abc );

};

