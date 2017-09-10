#pragma once

#include "..\..\Common\DMObject.h"

class DMRenderObject : public DMObject
{
public:
	DMRenderObject(  );
	~DMRenderObject();

	bool Update( float );
};

