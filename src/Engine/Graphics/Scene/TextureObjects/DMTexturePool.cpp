#include "DMTexturePool.h"

DMTexturePool* DMTexturePool::m_instance = nullptr;

void DMTexturePool::close()
{
	if( DMTexturePool::m_instance )
		delete DMTexturePool::m_instance;

	DMTexturePool::m_instance = nullptr;
}

DMTexturePool& DMTexturePool::instance()
{
	if( !m_instance )
		m_instance = new DMTexturePool();

	return *m_instance;
}

DMTexturePool::DMTexturePool()
{

}

DMTexturePool::~DMTexturePool()
{

}

bool DMTexturePool::initialize( const std::wstring& def_texture_file )
{
	DMTexture texture;
	if( texture.Initialize( def_texture_file.data() )  )
	{
		m_texure_pool.push_back( std::move( texture ) );
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

	DMTexture texture;
	if( texture.Initialize( file.data() ) )
	{
		m_texure_pool.push_back( std::move( texture ) );
		m_path_to_index[file] = m_texure_pool.size() - 1;

		return m_path_to_index[file];
	}

	return 0;
}

ID3D11ShaderResourceView* DMTexturePool::texture( unsigned int index )
{
	if( m_texure_pool.size() > index )
		return m_texure_pool[index].GetTexture();

	return nullptr;
}