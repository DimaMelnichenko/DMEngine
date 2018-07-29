#pragma once


#include <memory>

#include "..\Shaders\DMComputeShader.h"

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

private:
	struct ParticleData
	{
		XMFLOAT4 position;
		XMFLOAT4 velocity;
	};

private:
	unsigned int m_max_count;	
	com_unique_ptr<ID3D11Buffer> m_structuredBuffer;
	com_unique_ptr<ID3D11ShaderResourceView> m_srvParticles;
	com_unique_ptr<ID3D11UnorderedAccessView> m_uavParticles;
	XMMATRIX m_world_matrix;

	DMComputeShader m_computeShader;

};

