#pragma once


#include <memory>

#include "..\Shaders\DMComputeShader.h"
#include "Properties/PropertyContainer.h"

class DMParticleSystem
{
public:
	DMParticleSystem();
	~DMParticleSystem();

	bool Initialize( unsigned int max_count, unsigned int width );
	void Render();
	unsigned int particleCount();
	

	XMMATRIX* resultMatrix();

	void update( float elapsedTime );

	void generate();
	void after_generate();

	PropertyContainer m_propertyContainer;

	struct ParticleParams
	{
		XMFLOAT4 heightMultipler;
		XMFLOAT4 highOfDeath;
	};

private:
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

};

