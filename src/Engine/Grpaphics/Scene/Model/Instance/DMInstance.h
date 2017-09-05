#pragma once

#include "DMModel.h"


class DMInstance : public DM3DObject
{
public:
	DMInstance( DMD3D* dmd3d );
	~DMInstance();

	enum INSTANCE_TYPE
	{
		INST_POS, INST_SCALE, INST_TEX
	};

	bool initialize( std::unique_ptr<DMModel>&, INSTANCE_TYPE type, unsigned int max_count );

	struct alignas(16) InstanceDataType
	{
		D3DXVECTOR3 pos;
		float scale;
		//D3DXVECTOR2 tex;
	};

	void set_data( const std::vector<DMInstance::InstanceDataType>& );

	void set_to_draw();

	unsigned int count();

	DMModel* model();

private:
	

private:
	std::unique_ptr<DMModel> m_model;
	com_unique_ptr<ID3D11Buffer> m_instance_buffer;
	com_unique_ptr<ID3D11ShaderResourceView> m_instance_view;	
	INSTANCE_TYPE m_type;
	unsigned int m_max_count;
	unsigned int m_current_count;
};

