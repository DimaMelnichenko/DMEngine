#pragma once

#include <vector>

#include "..\Shaders\DMShader.h"
#include "..\Model\DMModel.h"

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

