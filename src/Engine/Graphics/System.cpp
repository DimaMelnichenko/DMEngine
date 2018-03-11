#include "System.h"

namespace GS
{

static std::unique_ptr<DMTextureStorage> m_textureStorage = nullptr;

DMTextureStorage& System::textures()
{
	if( !m_textureStorage )
	{
		m_textureStorage = std::make_unique<DMTextureStorage>( L"Textures" );
	}

	return *m_textureStorage;
}



System::System()
{
}


System::~System()
{
}

}