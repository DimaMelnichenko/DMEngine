#pragma once
//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <vector>
#include <memory>
#include <unordered_set>

#include "..\TextureObjects\DMTextureStorage.h"
#include "Mesh\DMMesh.h"
#include "Common\DMTransformBuffer.h"
#include "Materials\Parameter\Parameter.h"

namespace GS
{

class DMModel : public DMResource
{
public:
	struct LodBlock
	{
		uint32_t mesh;
		uint32_t material;
		ParamSet params;
		const D3DXMATRIX* resultMatrix;
	};
public:
	DMModel( uint32_t id, const std::string& name = "" );
	DMModel( DMModel&& );
	DMModel& operator=( DMModel&& );
	~DMModel();

	void addLod( float range, const LodBlock& );
	const LodBlock* getLod( float range );

	const DMTransformBuffer& transformBuffer() const;
	DMTransformBuffer& transformBuffer();

	void copyTo( DMModel& );
private:
	
	std::vector<std::pair<float, LodBlock>> m_lods;

	DMTransformBuffer m_transformBuffer;
};

}