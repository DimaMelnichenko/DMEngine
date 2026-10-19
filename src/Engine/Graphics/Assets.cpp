#include "Assets.h"

namespace GS
{

namespace
{
	static std::unique_ptr<DMTextureStorage> m_textureStorage;
	static std::unique_ptr<MeshStorage> m_meshStorage;
	static std::unique_ptr<ModelStorage> m_modelStorage;
	static std::unique_ptr<MaterialStorage> m_materialStorage;
	static std::unique_ptr<MaterialInstanceStorage> m_materialInstances;
}

DMTextureStorage& Assets::textures()
{
	if( !m_textureStorage )
	{
		m_textureStorage = std::make_unique<DMTextureStorage>( "Textures" );
	}

	return *m_textureStorage;
}
 
MeshStorage& Assets::meshes()
{
	if( !m_meshStorage )
	{
		m_meshStorage = std::make_unique<MeshStorage>( "Meshes" );
	}

	return *m_meshStorage;
}

ModelStorage& Assets::models()
{
	if( !m_modelStorage )
	{
		m_modelStorage = std::make_unique<ModelStorage>( "Models" );
	}

	return *m_modelStorage;
}

MaterialStorage& Assets::materials()
{
	if( !m_materialStorage )
	{
		m_materialStorage = std::make_unique<MaterialStorage>( "Shaders" );
	}

	return *m_materialStorage;
}

MaterialInstanceStorage& Assets::materialInstances()
{
	if( !m_materialInstances )
	{
		m_materialInstances = std::make_unique<MaterialInstanceStorage>();
	}

	return *m_materialInstances;
}

void Assets::destroy()
{
	m_textureStorage.reset();
	m_meshStorage.reset();
	m_modelStorage.reset();
	m_materialStorage.reset();
	m_materialInstances.reset();
}

}