#include "System.h"

namespace GS
{

std::unique_ptr<DMTextureStorage> System::m_textureStorage = nullptr;
std::unique_ptr<MeshStorage> System::m_meshStorage = nullptr;
std::unique_ptr<ModelStorage> System::m_modelStorage = nullptr;
std::unique_ptr<MaterialStorage> System::m_materialStorage = nullptr;

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
		m_materialStorage = std::make_unique<MaterialStorage>( "Materials" );
	}

	return *m_materialStorage;
}


System::System()
{
}


System::~System()
{
}

}