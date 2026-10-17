#include "CubePrefilter.h"
#include "Shaders\slots.h"
#include "Shaders\cube_prefilter_constants.h"
#include "D3D\DMD3D.h"

namespace GS
{

bool CubePrefilter::initialize( DXGI_FORMAT resultFormat )
{
	return m_shader.load( "Shaders\\cube_prefilter.ps", TargetFormats::colorTarget( resultFormat ) ) &&
		   m_mipShader.initialize( "Shaders\\cube_downsample.cs", "main" ) &&
		   DMD3D::instance().createShaderConstantBuffer( sizeof( PrefilterParameters ), m_constantBuffer );
}

void CubePrefilter::buildMips( const CubeTarget& source )
{
	DMD3D& d3d = DMD3D::instance();
	for( uint32_t mip = 1; mip < source.mipCount(); ++mip )
	{
		d3d.beginPass( PassDesc{ "Cube mip", {}, {}, 0, 0, { { &source.mipView( mip - 1 ), "cube mip N" } },
								 { { &source.mipStorage( mip ), "cube mip N + 1" } } } );
		d3d.setSRV( 0, source.mipView( mip - 1 ) );
		m_mipShader.setUAVBuffer( 0, source.mipStorage( mip ) );
		const uint32_t groups = ( source.mipSize( mip ) + 7 ) / 8;
		m_mipShader.dispatchGroups( groups, groups, 6 );
	}
}

void CubePrefilter::prefilterFace( const CubeTarget& source, const CubeTarget& result, int32_t face, float outputScale )
{
	// Мип m — шероховатость m / (мипов − 1)
	DMD3D& d3d = DMD3D::instance();
	PrefilterParameters params = {};
	params.face = face;
	params.sourceSize = static_cast<float>( source.size() );
	params.outputScale = outputScale;
	const uint32_t mipCount = result.mipCount();
	for( uint32_t mip = 0; mip < mipCount; ++mip )
	{
		params.roughness = mipCount > 1 ? static_cast<float>( mip ) / ( mipCount - 1 ) : 0.0f;
		Device::updateResourceData<PrefilterParameters>( m_constantBuffer, params );
		d3d.setConstantBuffer( SLOT_CB_PASS, m_constantBuffer );
		const uint32_t size = result.mipSize( mip );
		d3d.beginPass( PassDesc{ "Cube prefilter", { { &result.target( mip, face ), "prefiltered cube" } }, {}, size, size,
								 { { &source.srv(), "source cube" } } } );
		d3d.setSRV( 0, source.srv() );
		m_shader.draw();
	}
}

}
