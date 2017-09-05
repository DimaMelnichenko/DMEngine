#include "DMTexturePool.h"



DMTexturePool::DMTexturePool( DMD3D* dmd3d ) : DM3DObject( dmd3d )
{

}

DMTexturePool::~DMTexturePool()
{

}

bool DMTexturePool::initialize( const std::wstring& def_texture_file )
{
	DMTexture* texture = new DMTexture( m_dmd3d );
	if( texture->Initialize( def_texture_file.data() )  )
	{
		m_texure_pool.push_back( std::move( std::unique_ptr<DMTexture>( texture ) ) );
		m_path_to_index[def_texture_file] = m_texure_pool.size() - 1;

		return true;
	}

	return false;
}

unsigned int DMTexturePool::load_texture( const std::wstring& file )
{
	if( m_path_to_index.count( file ) )
	{
		return m_path_to_index[file];
	}

	DMTexture* texture = new DMTexture( m_dmd3d );
	if( texture->Initialize( file.data() ) )
	{
		m_texure_pool.push_back( std::move( std::unique_ptr<DMTexture>( texture ) ) );
		m_path_to_index[file] = m_texure_pool.size() - 1;

		return m_path_to_index[file];
	}

	return 0;
}

ID3D11ShaderResourceView* DMTexturePool::texture( unsigned int index )
{
	if( m_texure_pool.size() > index )
		return m_texure_pool[index]->GetTexture();

	return nullptr;
}