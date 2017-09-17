#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <stdlib.h>
#include <time.h>




class DMModelList
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

