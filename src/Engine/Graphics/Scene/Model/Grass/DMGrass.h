#pragma once

#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\DMComputeShader.h"
#include "../DMModel.h"
#include "GrassLod.h"

namespace GS
{

class DMGrass
{
public:
	DMGrass();
	~DMGrass();

	bool Initialize();
	bool addMesh( DMModel::LodBlock* );

	void prerender();
	void Render( uint16_t lod, uint16_t instanceSlot );
	
	uint16_t lodCount();
	DMModel::LodBlock* lodBlock( uint16_t );
	ID3D11ShaderResourceView* vertexBuffer( uint16_t lod );
	ID3D11Buffer* indirectArgsBuffer( uint16_t lod );
	
private:
	struct LodStruct
	{
		DMModel::LodBlock* lodBlock;
		GrassLod grassLod;
	};
	std::unordered_map<uint16_t, LodStruct> m_lods;
	DMComputeShader m_computeShader;
	DMComputeShader m_initShader;
};

}