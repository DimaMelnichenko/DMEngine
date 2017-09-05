#pragma once
#include "DM3DObject.h"

class DMEnvironment : public DM3DObject
{
public:
	DMEnvironment( DMD3D* );
	~DMEnvironment();

	void Inititalize( unsigned int texture_size );

private:
	com_unique_ptr<ID3D11Texture2D> m_cube_texture;
};

