#pragma once

#include "DMObject.h"
#include "DMModel.h"
#include "DMShader.h"
#include <vector>


class DMDrawContainer
{
public:
	DMDrawContainer( DMShader* );
	~DMDrawContainer();	
	void addModel( DMModel* );
	DMShader* shader();	
	std::vector<DMModel*> m_models;
	void update( float );

private:
	DMShader* m_shader;
};

