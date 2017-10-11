#pragma once

#include "D3D/DMD3D.h"
#include "../DMModel.h"
#include "D3D/DMStructuredBuffer.h"

enum INSTANCE_TYPE
{
	INST_POS, INST_SCALE, INST_TEXW
};

class DMInstance
{
public:
	DMInstance();
	~DMInstance();

	bool initialize( const DMModel& model, INSTANCE_TYPE type, unsigned int max_count );
	bool initialize( DMModel&& model, INSTANCE_TYPE type, unsigned int max_count );

	struct alignas(16) VSInstanceDataType
	{
		D3DXVECTOR3 pos;
		float scale;
		//D3DXVECTOR2 tex;
	};

	struct alignas( 16 ) PSInstanceDataType
	{
		D3DXVECTOR4 albedo;
		D3DXVECTOR4 fresnel_gloss;
	};

	void set_data( const std::vector<DMInstance::VSInstanceDataType>& );

	void set_to_draw();

	unsigned int count();

	const DMModel& model();

private:
	bool initializeBuffers( INSTANCE_TYPE type, unsigned int max_count );

private:
	DMModel m_model;
	INSTANCE_TYPE m_type;
	unsigned int m_max_count;
	unsigned int m_current_count;
	DMStructuredBuffer m_vsStrcturedBuffer;
	DMStructuredBuffer m_psStrcturedBuffer;
};

