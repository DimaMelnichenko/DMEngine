#pragma once


#include <memory>
#include <string>

#include "..\Shaders\DMComputeShader.h"
#include "Properties/PropertyContainer.h"
#include "SceneObject.h"

class DMParticleSystem : public GS::SceneObject
{
public:
	DMParticleSystem();
	~DMParticleSystem();

	// Частицы над квадратом width × width у начала координат, max_count на клетку; имена — в хранилищах текстур
	// и материалов. Высота гибели частицы отсчитывается от карты высот террейна
	bool Initialize( unsigned int max_count, unsigned int width, const std::string& heightMap,
					 const std::string& material, const std::string& texture );
	unsigned int particleCount();

	void compute( const GS::FrameContext& frame ) override;
	void render( const GS::FrameContext& frame, GS::RenderPass pass ) override;
	PropertyContainer* properties() override;

	PropertyContainer m_propertyContainer;

	struct ParticleParams
	{
		XMFLOAT4 heightMultiplier;
		XMFLOAT4 highOfDeath;
	};

private:
	void update( float elapsedTime );
	void bindParticles();

	struct ParticleData
	{
		XMFLOAT3 position;
		XMFLOAT3 velocity;
		XMFLOAT2 dummy;
	};

private:
	unsigned int m_max_count;	
	com_unique_ptr<ID3D11Buffer> m_structuredBuffer;
	com_unique_ptr<ID3D11ShaderResourceView> m_srvParticles;
	com_unique_ptr<ID3D11UnorderedAccessView> m_uavParticles;
	XMMATRIX m_world_matrix;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;

	DMComputeShader m_computeShader;
	std::string m_heightMap;
	std::string m_material;
	std::string m_texture;
	bool m_initialized = false;

};

