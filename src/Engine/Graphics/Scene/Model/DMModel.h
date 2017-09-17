#pragma once
//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <vector>
#include <memory>

#include "..\TextureObjects\DMTexturePool.h"
#include "..\Model\DMMesh.h"
#include "..\Model\DMRenderQueue.h"

class DMModel :	public DMSceneObject
{
public:
	DMModel( DMD3D*, DMTexturePool* );
	~DMModel();

	enum TextureType
	{
		albedo, normal, height, gim
	};


	bool Initialize( float lod_range, DMMesh::VertexCombination, WCHAR* modelFilename );
	bool addLODModel( float lod_range, DMMesh::VertexCombination, WCHAR* modelFilename );
	void Render( float lod_range );
	void Render( DMCamera* );
	void addRenderQueue( DMRenderQueue* );
	

	unsigned int texture( DMModel::TextureType type );
	void setTexure( TextureType type, unsigned int, float u_scale = 1.0, float v_scale = 1.0 );
	void setTexures();
	void textureScale( unsigned int index, D3DXVECTOR2* uv_sacle ) const;

	void setPos( float, float, float );
	void setPos( const D3DXVECTOR3 & );
	void setScale( float );
	void setInFrustum( float );
	void setInFrustum( DMCamera* );

	unsigned long GetIndexCount();

	// копирование и присваивание
	DMModel( const DMModel* );
	DMModel( const DMModel& );
	DMModel* operator=( const DMModel* );
	DMModel& operator=( const DMModel& );

private:
	void Shutdown();
	ID3D11ShaderResourceView* LoadTexture( const WCHAR* );
	void copy_internal_data( const DMModel* model );
	

private:
	struct RangeMeshesContainer
	{
		float range;
		std::shared_ptr<DMMesh> mesh;		
	};
	// переменные общие
	std::shared_ptr<std::vector<RangeMeshesContainer>> m_meshes;
	unsigned int m_current_mesh_index;
	std::shared_ptr<std::vector<unsigned int>> m_textures;
	std::shared_ptr<std::vector<D3DXVECTOR2>> m_textures_scale;	
	std::shared_ptr<std::vector<DMRenderQueue*>> m_render_queues;
	DMTexturePool* m_texture_pool;
};

