#pragma once

//////////////
// INCLUDES //
//////////////
#include <d3d11.h>
#include <d3dx10math.h>
#include <stdlib.h>
#include <time.h>

#include "..\..\Common\DMObject.h"


class DMModelList :	public DMObject
{
private:
	struct ModelInfoType
	{
		D3DXVECTOR4 color;
		float positionX, positionY, positionZ;
	};

public:
	DMModelList(  );
	~DMModelList();

	bool Initialize( int );
	void Shutdown( );

	int GetModelCount( );
	void GetData( int, float&, float&, float&, D3DXVECTOR4& );

private:
	int m_modelCount;
	ModelInfoType* m_ModelInfoList;

};

