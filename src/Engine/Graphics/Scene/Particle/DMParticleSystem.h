#pragma once


#include <memory>
#include <string>

#include "DMComputeShader.h"
#include "Properties/PropertyContainer.h"
#include "SceneObject.h"

class DMParticleSystem : public GS::SceneObject
{
public:
	DMParticleSystem();
	~DMParticleSystem();

	// Частицы над квадратом width × width у начала координат, max_count на клетку; имена — в хранилищах текстур
	// и материалов. Высота гибели частицы отсчитывается от карты высот террейна (TerrainHeight::heightMap)
	bool Initialize( unsigned int max_count, unsigned int width, const ShaderView* heightMap,
					 const std::string& material, const std::string& texture );
	unsigned int particleCount();

	void compute( const GS::FrameContext& frame ) override;
	void collectMeshes( const GS::RenderView& view, GS::MeshCollector& collector ) override;
	void renderCustom( const GS::RenderContext& context ) override;
	PropertyContainer* properties() override;

	PropertyContainer m_propertyContainer;

	struct ParticleParams
	{
		DirectX::XMFLOAT4 heightMultiplier;
		DirectX::XMFLOAT4 highOfDeath;
	};

private:
	void update( float elapsedTime );
	void bindParticles();

	struct ParticleData
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 velocity;
		DirectX::XMFLOAT2 dummy;
	};

private:
	unsigned int m_max_count;	
	Buffer m_structuredBuffer;
	ShaderView m_srvParticles;
	StorageView m_uavParticles;
	DirectX::XMMATRIX m_world_matrix;
	Buffer m_constantBuffer;

	DMComputeShader m_computeShader;
	const ShaderView* m_heightMap = nullptr;
	std::string m_material;
	std::string m_texture;
	bool m_initialized = false;

};

