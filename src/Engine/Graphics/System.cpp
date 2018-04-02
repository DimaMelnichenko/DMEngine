#include "System.h"

namespace GS
{

static std::unique_ptr<DMTextureStorage> m_textureStorage = nullptr;
static std::unique_ptr<MeshStorage> m_meshStorage = nullptr;
static std::unique_ptr<ModelStorage> m_modelStorage = nullptr;
static std::unique_ptr<MaterialStorage> m_materialStorage = nullptr;

DMTextureStorage& System::textures()
{
	if( !m_textureStorage )
	{
		m_textureStorage = std::make_unique<DMTextureStorage>( L"Textures" );
	}

	return *m_textureStorage;
}

MeshStorage& System::meshes()
{
	if( !m_meshStorage )
	{
		m_meshStorage = std::make_unique<MeshStorage>( L"Meshes" );
	}

	return *m_meshStorage;
}

ModelStorage& System::models()
{
	if( !m_modelStorage )
	{
		m_modelStorage = std::make_unique<ModelStorage>( L"Models" );
	}

	return *m_modelStorage;
}

MaterialStorage& System::materials()
{
	if( !m_materialStorage )
	{
		m_materialStorage = std::make_unique<MaterialStorage>( L"Materials" );
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