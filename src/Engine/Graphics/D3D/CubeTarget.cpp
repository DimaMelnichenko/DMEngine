#include "CubeTarget.h"
#include "DMD3D.h"

bool CubeTarget::create( uint32_t size, uint32_t mipCount, DXGI_FORMAT format, bool computeMips, const char* name )
{
	DMD3D& d3d = DMD3D::instance();

	TextureDesc desc;
	desc.width = size;
	desc.height = size;
	desc.arraySize = 6;
	desc.mipCount = mipCount;
	desc.format = format;
	desc.usage = TextureUsage::renderTarget | TextureUsage::shaderResource | ( computeMips ? TextureUsage::unorderedAccess : 0 );
	desc.cube = true;
	if( !d3d.createTexture( desc, nullptr, m_texture ) )
		return false;
	if( name )
		d3d.setName( m_texture, name );

	// Цель — одна грань одного мипа
	m_targets.clear();
	m_targets.resize( m_texture.mipCount() * 6 );
	for( uint32_t mip = 0; mip < m_texture.mipCount(); ++mip )
	{
		for( uint32_t face = 0; face < 6; ++face )
		{
			TextureViewDesc target;
			target.kind = TextureViewDesc::Kind::texture2DArray;
			target.firstMip = mip;
			target.firstSlice = face;
			target.sliceCount = 1;
			if( !d3d.createTargetView( m_texture, target, m_targets[mip * 6 + face] ) )
				return false;
		}
	}

	TextureViewDesc cube;
	cube.kind = TextureViewDesc::Kind::cube;
	if( !d3d.createShaderView( m_texture, cube, m_srv ) )
		return false;

	// Цепочка мипов в compute: мип N читается как массив граней, мип N + 1 пишется через UAV
	m_mipViews.clear();
	m_mipStorage.clear();
	if( computeMips )
	{
		m_mipViews.resize( m_texture.mipCount() );
		m_mipStorage.resize( m_texture.mipCount() );
		for( uint32_t mip = 0; mip < m_texture.mipCount(); ++mip )
		{
			TextureViewDesc faces;
			faces.kind = TextureViewDesc::Kind::texture2DArray;
			faces.firstMip = mip;
			faces.mipCount = 1;
			faces.firstSlice = 0;
			faces.sliceCount = 6;
			if( !d3d.createShaderView( m_texture, faces, m_mipViews[mip] ) || !d3d.createStorageView( m_texture, faces, m_mipStorage[mip] ) )
				return false;
		}
	}
	return true;
}

bool CubeTarget::createFacesView( uint32_t mip )
{
	TextureViewDesc faces;
	faces.kind = TextureViewDesc::Kind::texture2DArray;
	faces.firstMip = mip;
	faces.mipCount = 1;
	faces.firstSlice = 0;
	faces.sliceCount = 6;
	return DMD3D::instance().createShaderView( m_texture, faces, m_facesSRV );
}
