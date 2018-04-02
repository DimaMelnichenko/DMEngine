#pragma once
//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <vector>
#include <memory>
#include <unordered_set>

#include "../DMSceneObject.h"
#include "..\TextureObjects\DMTextureStorage.h"
#include "..\Model\Mesh\DMMesh.h"
#include "..\Model\DMRenderQueue.h"

namespace GS
{

class DMModel : DMResource
{
public:
	DMModel( uint32_t id, const std::wstring& name = L"" );
	DMModel( DMModel&& );
	DMModel& operator=( DMModel&& );
	~DMModel();

	void addLod( float range, uint32_t meshId, uint32_t materialId );
	bool getLod( float range, uint32_t& meshId, uint32_t& materialId );
private:

	struct LodBlock
	{
		uint32_t mesh;
		uint32_t material;
	};

	std::wstring m_name;
	std::unordered_map<float, LodBlock> m_lods;
};

}