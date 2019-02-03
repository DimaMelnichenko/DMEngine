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
#include "Properties/PropertyContainer.h"

namespace GS
{

class DMModel : public DMResource
{
public:
	struct LodBlock
	{
		uint32_t mesh;
		uint32_t material;
		PropertyContainer params;
		bool isRender;
		const XMMATRIX* resultMatrix;
	};
public:
	DMModel( uint32_t id, const std::string& name = "" );
	DMModel( DMModel&& );
	DMModel& operator=( DMModel&& );
	~DMModel();

	void addLod( float range, std::unique_ptr<LodBlock>&& lod );
	const LodBlock* getLod( float range );
	LodBlock* getLodById( uint16_t index );
	uint16_t lodCount();

	const DMTransformBuffer& transformBuffer() const;
	DMTransformBuffer& transformBuffer();

	PropertyContainer* properties();

	void copyTo( DMModel& );
private:
	std::vector<std::pair<float, std::shared_ptr<LodBlock>>> m_lods;
	PropertyContainer m_properties;
	DMTransformBuffer m_transformBuffer;
};

}