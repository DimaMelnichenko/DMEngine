#pragma once
#include "DM3DObject.h"

class DMScene : public DM3DObject
{
public:
	DMScene( DMD3D* );
	~DMScene();

	bool loadLevel( const std::wstring& file_name );


};

