
#include "DMHeightMap.h"
#include "D3D\DMD3D.h"

#include <assert.h>
#include <iostream>
#include <fstream>

DMHeightMap::DMHeightMap()
{

}
DMHeightMap::~DMHeightMap()
{
	
}

//--------------------------------------------------------------------------------------
HRESULT DMHeightMap::LoadTextureDataFromFile( const std::string& szFileName )
{
    // open the file
    HANDLE hFile = CreateFile( szFileName.data(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, NULL );
    if( INVALID_HANDLE_VALUE == hFile )
        return HRESULT_FROM_WIN32( GetLastError() );

    // Get the file size
    LARGE_INTEGER FileSize = {0};
    GetFileSizeEx( hFile, &FileSize );

    // File is too big for 32-bit allocation, so reject read
    if( FileSize.HighPart > 0 )
    {
        CloseHandle( hFile );
        return E_FAIL;
    }

    // Need at least enough data to fill the header and magic number to be a valid DDS
    if( FileSize.LowPart < (sizeof(DDS_HEADER)+sizeof(DWORD)) )
    {
        CloseHandle( hFile );
        return E_FAIL;
    }

    // create enough space for the file data
    m_pHeapData = std::unique_ptr<unsigned char>( new unsigned char[ FileSize.LowPart ] );
    if( !( m_pHeapData ) )
    {
        CloseHandle( hFile );
        return E_OUTOFMEMORY;
    }

    // read the data in
    DWORD BytesRead = 0;
    if( !ReadFile( hFile, m_pHeapData.get(), FileSize.LowPart, &BytesRead, NULL ) )
    {
        CloseHandle( hFile );        
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    if( BytesRead < FileSize.LowPart )
    {
        CloseHandle( hFile );        
        return E_FAIL;
    }

    // DDS files always start with the same magic number ("DDS ")
    DWORD dwMagicNumber = *( DWORD* )( m_pHeapData.get() );
    if( dwMagicNumber != DDS_MAGIC )
    {
        CloseHandle( hFile );        
        return E_FAIL;
    }

	memset( &m_pHeader, 0, sizeof( DDS_HEADER ) );
	memcpy( &m_pHeader, m_pHeapData.get() + sizeof( DWORD ), sizeof( DDS_HEADER ) );
	//reinterpret_cast<DDS_HEADER*>( m_pHeapData.get() + sizeof( DWORD ) );

    // Verify header to validate DDS file
    if( m_pHeader.dwSize != sizeof(DDS_HEADER) || m_pHeader.ddspf.dwSize != sizeof(DDS_PIXELFORMAT) )
    {
        CloseHandle( hFile );        
        return E_FAIL;
    }

    // Check for DX10 extension
    bool bDXT10Header = false;
    if ( ( m_pHeader.ddspf.dwFlags & DDS_FOURCC )
        && (MAKEFOURCC( 'D', 'X', '1', '0' ) == m_pHeader.ddspf.dwFourCC) )
    {
        // Must be long enough for both headers and magic value
        if( FileSize.LowPart < (sizeof(DDS_HEADER)+sizeof(DWORD)+sizeof(DDS_HEADER_DXT10)) )
        {
            CloseHandle( hFile );            
            return E_FAIL;
        }

        bDXT10Header = true;
    }

    // setup the pointers in the process request    
    INT offset = sizeof( DWORD ) + sizeof( DDS_HEADER )
                 + (bDXT10Header ? sizeof( DDS_HEADER_DXT10 ) : 0);
		
	unsigned char* temp_ptr = m_data.get();
	temp_ptr = m_pHeapData.get() + offset;
    m_pBitSize = FileSize.LowPart - offset;

    CloseHandle( hFile );

    return S_OK;
}

UINT DMHeightMap::BitsPerPixel( DXGI_FORMAT fmt )
{
    switch( fmt )
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 128;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 96;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return 64;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return 32;

    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
        return 16;

    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
        return 8;

    case DXGI_FORMAT_R1_UNORM:
        return 1;

    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return 4;

    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return 8;

    default:
        assert( FALSE ); // unhandled format
        return 0;
    }
}

void DMHeightMap::GetSurfaceInfo( UINT width, UINT height, DXGI_FORMAT fmt )
{
    UINT numBytes = 0;
    UINT rowBytes = 0;
    UINT numRows = 0;

    bool bc = true;
    int bcnumBytesPerBlock = 16;
    switch( fmt )
    {
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			bcnumBytesPerBlock = 8;
		break;

		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
		break;

		default:
			bc = false;
			break;
    }

    if( bc )
    {
        int numBlocksWide = 0;
        if( width > 0 )
            numBlocksWide = max( 1, width / 4 );
        int numBlocksHigh = 0;
        if( height > 0 )
            numBlocksHigh = max( 1, height / 4 );
        rowBytes = numBlocksWide * bcnumBytesPerBlock;
        numRows = numBlocksHigh;
    }
    else
    {
        UINT bpp = BitsPerPixel( fmt );
        rowBytes = ( width * bpp + 7 ) / 8; // round up to nearest byte
		//rowBytes = ( width * bpp ); // round up to nearest byte
        numRows = height;
    }
    numBytes = rowBytes * numRows;


	m_RowBytes = rowBytes;
}

//--------------------------------------------------------------------------------------
#define ISBITMASK( r,g,b,a ) ( ddpf.dwRBitMask == r && ddpf.dwGBitMask == g && ddpf.dwBBitMask == b && ddpf.dwABitMask == a )


DXGI_FORMAT DMHeightMap::GetDXGIFormat( const DDS_PIXELFORMAT& ddpf )
{
	if( ddpf.dwFlags & DDS_RGB )
	{
		switch( ddpf.dwRGBBitCount )
		{
			case 32:
				// DXGI_FORMAT_B8G8R8A8_UNORM_SRGB & DXGI_FORMAT_B8G8R8X8_UNORM_SRGB should be
				// written using the DX10 extended header instead since these formats require
				// DXGI 1.1
				//
				// This code will use the fallback to swizzle RGB to BGR in memory for standard
				// DDS files which works on 10 and 10.1 devices with WDDM 1.0 drivers
				//
				// NOTE: We don't use DXGI_FORMAT_B8G8R8X8_UNORM or DXGI_FORMAT_B8G8R8X8_UNORM
				// here because they were defined for DXGI 1.0 but were not required for D3D10/10.1

				if( ISBITMASK( 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000 ) )
					return DXGI_FORMAT_R8G8B8A8_UNORM;
				if( ISBITMASK( 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000 ) )
					return DXGI_FORMAT_R8G8B8A8_UNORM; // No D3DFMT_X8B8G8R8 in DXGI

				// Note that many common DDS reader/writers swap the
				// the RED/BLUE masks for 10:10:10:2 formats. We assumme
				// below that the 'correct' header mask is being used. The
				// more robust solution is to use the 'DX10' header extension and
				// specify the DXGI_FORMAT_R10G10B10A2_UNORM format directly
				if( ISBITMASK( 0x000003ff, 0x000ffc00, 0x3ff00000, 0xc0000000 ) )
					return DXGI_FORMAT_R10G10B10A2_UNORM;

				if( ISBITMASK( 0x0000ffff, 0xffff0000, 0x00000000, 0x00000000 ) )
					return DXGI_FORMAT_R16G16_UNORM;

				if( ISBITMASK( 0xffffffff, 0x00000000, 0x00000000, 0x00000000 ) )
					// Only 32-bit color channel format in D3D9 was R32F
					return DXGI_FORMAT_R32_FLOAT; // D3DX writes this out as a FourCC of 114
				break;

			case 24:
				// No 24bpp DXGI formats
				break;

			case 16:
				// 5:5:5 & 5:6:5 formats are defined for DXGI, but are deprecated for D3D10+

				if( ISBITMASK( 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000 ) )
					return DXGI_FORMAT_B5G6R5_UNORM;
				if( ISBITMASK( 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000 ) )
					return DXGI_FORMAT_B5G5R5A1_UNORM;
				if( ISBITMASK( 0x00007c00, 0x000003e0, 0x0000001f, 0x00000000 ) )
					return DXGI_FORMAT_B5G5R5A1_UNORM; // No D3DFMT_X1R5G5B5 in DXGI

				// No 4bpp or 3:3:2 DXGI formats
				break;
		}
	}
	else if( ddpf.dwFlags & DDS_LUMINANCE )
	{
		if( 8 == ddpf.dwRGBBitCount )
		{
			if( ISBITMASK( 0x000000ff, 0x00000000, 0x00000000, 0x00000000 ) )
				return DXGI_FORMAT_R8_UNORM; // D3DX10/11 writes this out as DX10 extension

			// No 4bpp DXGI formats
		}

		if( 16 == ddpf.dwRGBBitCount )
		{
			if( ISBITMASK( 0x0000ffff, 0x00000000, 0x00000000, 0x00000000 ) )
				return DXGI_FORMAT_R16_UNORM; // D3DX10/11 writes this out as DX10 extension
			if( ISBITMASK( 0x000000ff, 0x00000000, 0x00000000, 0x0000ff00 ) )
				return DXGI_FORMAT_R8G8_UNORM; // D3DX10/11 writes this out as DX10 extension
		}
	}
	else if( ddpf.dwFlags & DDS_ALPHA )
	{
		if( 8 == ddpf.dwRGBBitCount )
		{
			return DXGI_FORMAT_A8_UNORM;
		}
	}
	else if( ddpf.dwFlags & DDS_FOURCC )
	{
		if( MAKEFOURCC( 'D', 'X', 'T', '1' ) == ddpf.dwFourCC )
			return DXGI_FORMAT_BC1_UNORM;
		if( MAKEFOURCC( 'D', 'X', 'T', '3' ) == ddpf.dwFourCC )
			return DXGI_FORMAT_BC2_UNORM;
		if( MAKEFOURCC( 'D', 'X', 'T', '5' ) == ddpf.dwFourCC )
			return DXGI_FORMAT_BC3_UNORM;

		if( MAKEFOURCC( 'B', 'C', '4', 'U' ) == ddpf.dwFourCC )
			return DXGI_FORMAT_BC4_UNORM;
		if( MAKEFOURCC( 'B', 'C', '4', 'S' ) == ddpf.dwFourCC )
			return DXGI_FORMAT_BC4_SNORM;

		if( MAKEFOURCC( 'A', 'T', 'I', '2' ) == ddpf.dwFourCC )
			return DXGI_FORMAT_BC5_UNORM;
		if( MAKEFOURCC( 'B', 'C', '5', 'S' ) == ddpf.dwFourCC )
			return DXGI_FORMAT_BC5_SNORM;

		if( MAKEFOURCC( 'R', 'G', 'B', 'G' ) == ddpf.dwFourCC )
			return DXGI_FORMAT_R8G8_B8G8_UNORM;
		if( MAKEFOURCC( 'G', 'R', 'G', 'B' ) == ddpf.dwFourCC )
			return DXGI_FORMAT_G8R8_G8B8_UNORM;

		// Check for D3DFORMAT enums being set here
		switch( ddpf.dwFourCC )
		{
			case 36: //D3DFMT_A16B16G16R16: // 36
				return DXGI_FORMAT_R16G16B16A16_UNORM;

			case 110: //D3DFMT_Q16W16V16U16: // 110
				return DXGI_FORMAT_R16G16B16A16_SNORM;

			case 111: //D3DFMT_R16F: // 111
				return DXGI_FORMAT_R16_FLOAT;

			case 112: //D3DFMT_G16R16F: // 112
				return DXGI_FORMAT_R16G16_FLOAT;

			case 113: //D3DFMT_A16B16G16R16F: // 113
				return DXGI_FORMAT_R16G16B16A16_FLOAT;

			case 114: //D3DFMT_R32F: // 114
				return DXGI_FORMAT_R32_FLOAT;

			case 115: //D3DFMT_G32R32F: // 115
				return DXGI_FORMAT_R32G32_FLOAT;

			case 116: //D3DFMT_A32B32G32R32F: // 116
				return DXGI_FORMAT_R32G32B32A32_FLOAT;
		}
	}

	return DXGI_FORMAT_UNKNOWN;
}

//--------------------------------------------------------------------------------------
HRESULT DMHeightMap::CreateDDSTextureFromFile( const std::string& file_name )
{  

	HRESULT hr;
	if( FAILED( hr = LoadTextureDataFromFile( file_name ) ) )
		return S_FALSE;

	m_fmt = GetDXGIFormat( m_pHeader.ddspf );

	GetSurfaceInfo( m_pHeader.dwWidth, m_pHeader.dwHeight, m_fmt );

	ID3D11ShaderResourceView* texture;
	HRESULT result = D3DX11CreateShaderResourceViewFromFile( DMD3D::instance().GetDevice(), file_name.data(), NULL, NULL, &texture, NULL );
	if( FAILED( result ) )
	{
		return S_FALSE;
	}

	m_heightmap_srv = make_com_ptr<ID3D11ShaderResourceView>( texture );

    return hr;
}

float DMHeightMap::pixel( float x, float y )
{
	unsigned int intx = (unsigned int)x;
	unsigned int inty = (unsigned int)y;

	if( intx < 0 || inty < 0 || intx > m_pHeader.dwWidth || inty > m_pHeader.dwHeight )
		return 0;

	return *reinterpret_cast<float*>( m_data.get() + m_RowBytes * inty + ( intx * ( BitsPerPixel( m_fmt ) / 8 ) ) );
}

float DMHeightMap::height( float x, float y )
{
	if( x < 0 || y < 0 || x > size().x || y > size().y )
		return 0.0;

	y = size().y - y;

	D3DXVECTOR3 abc_1[3];
	D3DXVECTOR3 abc_2[3];
	D3DXVECTOR3 point;
	
	point.x = x;
	point.z = y;

	float control_x = floor( x );
	float control_z = floor( y );
	
	abc_1[0] = D3DXVECTOR3( control_x, pixel( control_x, control_z ), control_z );
	abc_1[1] = D3DXVECTOR3( control_x + 1, pixel( control_x + 1, control_z ), control_z );
	abc_1[2] = D3DXVECTOR3( control_x + 1, pixel( control_x + 1, control_z + 1 ), control_z + 1 );

	abc_2[0] = D3DXVECTOR3( control_x, pixel( control_x, control_z ), control_z );
	abc_2[1] = D3DXVECTOR3( control_x + 1, pixel( control_x + 1, control_z + 1 ), control_z + 1 );
	abc_2[2] = D3DXVECTOR3( control_x, pixel( control_x, control_z + 1 ), control_z + 1 );

	if( checkHeightOfTriangle( &point, abc_1 ) )
	{
		return point.y;
	}
	else if( checkHeightOfTriangle( &point, abc_2 ) )
	{
		return point.y;
	}

	return 0.0;
}

D3DXVECTOR2 DMHeightMap::size( )
{
	return D3DXVECTOR2( (float)m_pHeader.dwWidth, (float)m_pHeader.dwHeight );
}

bool DMHeightMap::in_triangle( D3DXVECTOR2* d, D3DXVECTOR2* abc )
{
	double xa, ya, xb, yb, xc, yc, xd, yd;

	xa = abc[0].x;
	ya = abc[0].y;
	xb = abc[1].x;
	yb = abc[1].y;
	xc = abc[2].x;
	yc = abc[2].y;
	xd = d->x;
	yd = d->y;

	return  ( ( ( xd - xa )*( yb - ya ) - ( yd - ya )*( xb - xa ) )*( ( xc - xa )*( yb - ya ) - ( yc - ya )*( xb - xa ) ) >= 0 ) &&
		( ( ( xd - xb )*( yc - yb ) - ( yd - yb )*( xc - xb ) )*( ( xa - xb )*( yc - yb ) - ( ya - yb )*( xc - xb ) ) >= 0 ) &&
		( ( ( xd - xc )*( ya - yc ) - ( yd - yc )*( xa - xc ) )*( ( xb - xc )*( ya - yc ) - ( yb - yc )*( xa - xc ) ) >= 0 );
}

bool DMHeightMap::checkHeightOfTriangle( D3DXVECTOR3* point, D3DXVECTOR3* abc )
{
	float startVector[3], directionVector[3], edge1[3], edge2[3], normal[3];
	float Q[3], e1[3], e2[3], e3[3], edgeNormal[3], temp[3];
	float magnitude, D, denominator, numerator, t, determinant;
	float v0[3], v1[3], v2[3];
	v0[0] = abc[0].x;
	v0[1] = abc[0].y;
	v0[2] = abc[0].z;
	v1[0] = abc[1].x;
	v1[1] = abc[1].y;
	v1[2] = abc[1].z;
	v2[0] = abc[2].x;
	v2[1] = abc[2].y;
	v2[2] = abc[2].z;

	// Starting position of the ray that is being cast.
	startVector[0] = point->x;
	startVector[1] = 0.0f;
	startVector[2] = point->z;

	// The direction the ray is being cast.
	directionVector[0] = 0.0f;
	directionVector[1] = -1.0f;
	directionVector[2] = 0.0f;

	// Calculate the two edges from the three points given.
	edge1[0] = v1[0] - v0[0];
	edge1[1] = v1[1] - v0[1];
	edge1[2] = v1[2] - v0[2];

	edge2[0] = v2[0] - v0[0];
	edge2[1] = v2[1] - v0[1];
	edge2[2] = v2[2] - v0[2];

	// Calculate the normal of the triangle from the two edges.
	normal[0] = ( edge1[1] * edge2[2] ) - ( edge1[2] * edge2[1] );
	normal[1] = ( edge1[2] * edge2[0] ) - ( edge1[0] * edge2[2] );
	normal[2] = ( edge1[0] * edge2[1] ) - ( edge1[1] * edge2[0] );

	magnitude = (float)sqrt( ( normal[0] * normal[0] ) + ( normal[1] * normal[1] ) + ( normal[2] * normal[2] ) );
	normal[0] = normal[0] / magnitude;
	normal[1] = normal[1] / magnitude;
	normal[2] = normal[2] / magnitude;

	// Find the distance from the origin to the plane.
	D = ( ( -normal[0] * v0[0] ) + ( -normal[1] * v0[1] ) + ( -normal[2] * v0[2] ) );

	// Get the denominator of the equation.
	denominator = ( ( normal[0] * directionVector[0] ) + ( normal[1] * directionVector[1] ) + ( normal[2] * directionVector[2] ) );

	// Make sure the result doesn't get too close to zero to prevent divide by zero.
	if( fabs( denominator ) < 0.0001f )
	{
		return false;
	}

	// Get the numerator of the equation.
	numerator = -1.0f * ( ( ( normal[0] * startVector[0] ) + ( normal[1] * startVector[1] ) + ( normal[2] * startVector[2] ) ) + D );

	// Calculate where we intersect the triangle.
	t = numerator / denominator;

	// Find the intersection vector.
	Q[0] = startVector[0] + ( directionVector[0] * t );
	Q[1] = startVector[1] + ( directionVector[1] * t );
	Q[2] = startVector[2] + ( directionVector[2] * t );

	// Find the three edges of the triangle.
	e1[0] = v1[0] - v0[0];
	e1[1] = v1[1] - v0[1];
	e1[2] = v1[2] - v0[2];

	e2[0] = v2[0] - v1[0];
	e2[1] = v2[1] - v1[1];
	e2[2] = v2[2] - v1[2];

	e3[0] = v0[0] - v2[0];
	e3[1] = v0[1] - v2[1];
	e3[2] = v0[2] - v2[2];

	// Calculate the normal for the first edge.
	edgeNormal[0] = ( e1[1] * normal[2] ) - ( e1[2] * normal[1] );
	edgeNormal[1] = ( e1[2] * normal[0] ) - ( e1[0] * normal[2] );
	edgeNormal[2] = ( e1[0] * normal[1] ) - ( e1[1] * normal[0] );

	// Calculate the determinant to see if it is on the inside, outside, or directly on the edge.
	temp[0] = Q[0] - v0[0];
	temp[1] = Q[1] - v0[1];
	temp[2] = Q[2] - v0[2];

	determinant = ( ( edgeNormal[0] * temp[0] ) + ( edgeNormal[1] * temp[1] ) + ( edgeNormal[2] * temp[2] ) );

	// Check if it is outside.
	if( determinant > 0.001f )
	{
		return false;
	}

	// Calculate the normal for the second edge.
	edgeNormal[0] = ( e2[1] * normal[2] ) - ( e2[2] * normal[1] );
	edgeNormal[1] = ( e2[2] * normal[0] ) - ( e2[0] * normal[2] );
	edgeNormal[2] = ( e2[0] * normal[1] ) - ( e2[1] * normal[0] );

	// Calculate the determinant to see if it is on the inside, outside, or directly on the edge.
	temp[0] = Q[0] - v1[0];
	temp[1] = Q[1] - v1[1];
	temp[2] = Q[2] - v1[2];

	determinant = ( ( edgeNormal[0] * temp[0] ) + ( edgeNormal[1] * temp[1] ) + ( edgeNormal[2] * temp[2] ) );

	// Check if it is outside.
	if( determinant > 0.001f )
	{
		return false;
	}

	// Calculate the normal for the third edge.
	edgeNormal[0] = ( e3[1] * normal[2] ) - ( e3[2] * normal[1] );
	edgeNormal[1] = ( e3[2] * normal[0] ) - ( e3[0] * normal[2] );
	edgeNormal[2] = ( e3[0] * normal[1] ) - ( e3[1] * normal[0] );

	// Calculate the determinant to see if it is on the inside, outside, or directly on the edge.
	temp[0] = Q[0] - v2[0];
	temp[1] = Q[1] - v2[1];
	temp[2] = Q[2] - v2[2];

	determinant = ( ( edgeNormal[0] * temp[0] ) + ( edgeNormal[1] * temp[1] ) + ( edgeNormal[2] * temp[2] ) );

	// Check if it is outside.
	if( determinant > 0.001f )
	{
		return false;
	}

	// Now we have our height.
	point->y = Q[1];

	return true;
}

HRESULT DMHeightMap::LoadRaw( const std::string& file_name, unsigned int width, unsigned int height, unsigned char bits )
{
	int i, j, index;
	FILE* filePtr;
	unsigned long long imageSize, count;
	std::vector<float> rawImage;

	m_fmt = DXGI_FORMAT_R32_FLOAT;

	memset( &m_pHeader, 0, sizeof( DDS_HEADER ) );

	m_pHeader.dwHeight = height;
	m_pHeader.dwWidth = width;
	m_RowBytes = m_pHeader.dwWidth * ( m_fmt / 8 );
	

	// Create the float array to hold the height map data.
	m_data = std::unique_ptr<unsigned char>( new unsigned char[m_pHeader.dwHeight * m_pHeader.dwWidth * ( m_fmt / 8 )] );

	// Open the raw height map file for reading in binary.	
	std::ifstream fin;
	try
	{
		fin.open( "Textures\\terrain\\heightmap.r16", std::ifstream::in | std::ifstream::binary );
	}
	catch( std::ifstream::failure e )
	{
		std::cerr << "Exception opening/reading/closing file\n";
	}
	
	fin.seekg( 0, fin.end );
	unsigned int length = fin.tellg();
	fin.seekg( 0, fin.beg );

	// Calculate the size of the raw image data.
	imageSize = m_pHeader.dwHeight * m_pHeader.dwWidth * ( bits / 8 );

	// Allocate memory for the raw image data.
	rawImage.resize( imageSize );
	

	// Read in the raw image data.
	float max_height = pow( 2, bits );
	count = 0;
	while( count < imageSize )
	{
		unsigned short val = 0;
		fin.read( (char*)&val, 2 );
		rawImage[count++] = (float)val / max_height;
	}

	// Close the file.	
	fin.close();

	// Copy the image data into the height map array.
	//memcpy( m_data.get(), &rawImage[0], imageSize );
	
	for( j = 0; j<m_pHeader.dwHeight; j++ )
	{
		for( i = 0; i<m_pHeader.dwWidth; i++ )
		{
			index = ( m_pHeader.dwWidth * j ) + i;
	
			// Store the height at this point in the height map array.
			unsigned char* raw_pointer = m_data.get() + m_RowBytes * j + ( i * ( BitsPerPixel( m_fmt ) / 8 ) );
			*reinterpret_cast<float*>( raw_pointer ) = rawImage[index];
		}
	}
	
	
	
	D3D11_TEXTURE2D_DESC desc;
	desc.ArraySize = 1;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.Format = m_fmt;
	desc.Height = m_pHeader.dwHeight;
	desc.Width = m_pHeader.dwWidth;
	desc.MipLevels = 1;
	desc.MiscFlags = 0;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;


	D3D11_SUBRESOURCE_DATA srd;
	srd.pSysMem = m_data.get();
	srd.SysMemPitch = m_RowBytes;
	srd.SysMemSlicePitch = m_pHeader.dwHeight * m_pHeader.dwWidth * (bits/8);

	ID3D11Texture2D* texture;
	HRESULT result = DMD3D::instance().GetDevice()->CreateTexture2D( &desc, &srd, &texture );
	if( FAILED(result) )
	{
		return result;
	}

	m_heightmap_texture = make_com_ptr<ID3D11Texture2D>( texture );


	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
	srv_desc.Texture2D.MipLevels = 1;
	srv_desc.Texture2D.MostDetailedMip = 0;
	srv_desc.Format = m_fmt;
	srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

	ID3D11ShaderResourceView* srv;
	result = DMD3D::instance().GetDevice()->CreateShaderResourceView( m_heightmap_texture.get(), &srv_desc, &srv );
	if( FAILED( result ) )
	{
		return result;
	}

	m_heightmap_srv = make_com_ptr<ID3D11ShaderResourceView>( srv );

	return true;
}

HRESULT DMHeightMap::LoadMap( const std::string& file_name )
{
	HRESULT hr;
	if( FAILED( hr = CreateDDSTextureFromFile( file_name ) ) )
		return S_FALSE;

	//return LoadStandartFile( file_name );

	return S_OK;
}

HRESULT DMHeightMap::LoadStandartFile( const std::string& file_name )
{
	HRESULT hr;

	D3DX11_IMAGE_INFO info;
	if( FAILED( hr = D3DX11GetImageInfoFromFile( file_name.data(), nullptr, &info, nullptr ) ) )
		return hr;

	m_pHeader.dwHeight = info.Height;
	m_pHeader.dwWidth = info.Width;
	
	ID3D11ShaderResourceView* srv;
	if( FAILED( hr = D3DX11CreateShaderResourceViewFromFile( DMD3D::instance().GetDevice(), file_name.data(),
															 nullptr, nullptr, &srv, nullptr ) ) )
	{
		return S_FALSE;
	}

	m_heightmap_srv = make_com_ptr<ID3D11ShaderResourceView>( srv );

}

ID3D11ShaderResourceView* DMHeightMap::map()
{
	return m_heightmap_srv.get();
}