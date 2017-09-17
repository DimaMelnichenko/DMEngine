#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <fstream>
#include "DMShader.h"


class DMColorShader : public DMShader
{
private:	

public:
	DMColorShader( DMD3D* );
	~DMColorShader();

	virtual void Shutdown( );
	virtual void Update( float );

private:
	virtual void prepareRender(  );;
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );	

};



