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


class DMModel
{
public:
	DMModel( const std::wstring& name = L"" );
	~DMModel();

	enum TextureType
	{
		albedo, normal, height, gim
	};
	
	bool Initialize( float lod_range, DMMesh::VertexCombination, WCHAR* modelFilename );
	const std::wstring& name();
	bool addLODModel( float lod_range, DMMesh::VertexCombination, WCHAR* modelFilename );
	unsigned int countOfLOD();
	void Render( float lod_range );

	unsigned int texture( DMModel::TextureType type );
	void setTexure( TextureType type, unsigned int, float u_scale = 1.0, float v_scale = 1.0 );
	void setTexures();
	void textureScale( unsigned int index, D3DXVECTOR2* uv_sacle ) const;

	void setInFrustum( float );

	unsigned long GetIndexCount();

	// копирование и присваивание
	DMModel( const DMModel* );
	DMModel( const DMModel& );
	DMModel* operator=( const DMModel* );
	DMModel& operator=( const DMModel& );

	DMSceneObject& dceneObject();

private:
	ID3D11ShaderResourceView* LoadTexture( const WCHAR* );
	void copy_internal_data( const DMModel* model );
	
private:
	std::wstring m_name;
	unsigned int m_current_mesh_index;
	std::unordered_map<uint32_t, uint32_t> m_meshLodIndex;
	std::unordered_map<uint32_t, std::unique_ptr<DMMesh>> m_meshes;
	DMSceneObject m_sceneObject;
};

