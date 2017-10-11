#pragma once

#include <vector>

#include "..\Shaders\DMShader.h"
#include "..\Model\DMModel.h"

class DMDrawContainer
{
public:
	DMDrawContainer( DMShader* = nullptr );
	~DMDrawContainer();	
	void addModel( DMModel* );
	void setShader( DMShader* );
	DMShader* shader() const;	
	std::vector<DMModel*> m_models;
	void update( float );

private:
	DMShader* m_shader;
};

