#pragma once

#include "DMSceneObject.h"
#include "DMHeightMap.h"
#include <memory>
#include "DMComputeShader.h"


class DMParticleSystem : public DMSceneObject
{
public:
	DMParticleSystem( DMD3D* );
	~DMParticleSystem();

	bool Initialize( unsigned int max_count, unsigned int width );
	void Render();
	unsigned int particleCount();
	

	D3DXMATRIX* resultMatrix();

	void update( std::unique_ptr<DMComputeShader>&, float );

	void generate();
	void after_generate();

private:
	struct ParticleData
	{
		D3DXVECTOR4 position;
		D3DXVECTOR4 velocity;
	};

	HRESULT createBufferUAV();

private:
	unsigned int m_max_count;	
	com_unique_ptr<ID3D11Buffer> m_structured_buffer_0;
	com_unique_ptr<ID3D11Buffer> m_structured_buffer_1;
	com_unique_ptr<ID3D11ShaderResourceView> m_SRV_0;
	com_unique_ptr<ID3D11ShaderResourceView> m_SRV_1;
	com_unique_ptr<ID3D11UnorderedAccessView> m_UAV_0;
	com_unique_ptr<ID3D11UnorderedAccessView> m_UAV_1;
	D3DXMATRIX m_world_matrix;

};

