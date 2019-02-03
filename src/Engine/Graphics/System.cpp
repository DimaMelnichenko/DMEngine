#include "System.h"

namespace GS
{

namespace
{
	static std::unique_ptr<DMTextureStorage> m_textureStorage;
	static std::unique_ptr<MeshStorage> m_meshStorage;
	static std::unique_ptr<ModelStorage> m_modelStorage;
	static std::unique_ptr<MaterialStorage> m_materialStorage;
}

DMTextureStorage& System::textures()
{
	if( !m_textureStorage )
	{
		m_textureStorage = std::make_unique<DMTextureStorage>( "Textures" );
	}

	return *m_textureStorage;
}
 
MeshStorage& System::meshes()
{
	if( !m_meshStorage )
	{
		m_meshStorage = std::make_unique<MeshStorage>( "Meshes" );
	}

	return *m_meshStorage;
}

ModelStorage& System::models()
{
	if( !m_modelStorage )
	{
		m_modelStorage = std::make_unique<ModelStorage>( "Models" );
	}

	return *m_modelStorage;
}

MaterialStorage& System::materials()
{
	if( !m_materialStorage )
	{
		m_materialStorage = std::make_unique<MaterialStorage>( "Shaders" );
	}

	return *m_materialStorage;
}

System::System()
{
}


System::~System()
{
}

void System::destroy()
{
	m_textureStorage.reset();
	m_meshStorage.reset();
	m_modelStorage.reset();
	m_materialStorage.reset();
}

}