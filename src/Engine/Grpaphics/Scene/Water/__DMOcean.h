#pragma once

#include <d3d11.h>
#include <d3dx10math.h>
#include "DMObject.h"
#include "dmterrain.h"
#include "dmfrustum.h"
#include "dmterrainshader.h"
#include <map>
#include <vector>
#include <memory>
#include "DMCamera.h"
#include "DMTimer.h"
#include "DMD3D.h"
#include "DMOceanSimulator.h"
#include "DMTexture.h"

#pragma warning(disable:4995)
#define FRESNEL_TEX_SIZE			256
#define PERLIN_TEX_SIZE				64

class DMOcean : public DMObject
{
private:	

	struct QuadNode
	{
		D3DXVECTOR2 bottom_left;
		float length;
		int lod;

		int sub_node[4];
	};

	
	struct ocean_vertex
	{
		float index_x;
		float index_y;
	};

	struct QuadRenderParam
	{
		UINT num_inner_verts;
		UINT num_inner_faces;
		UINT inner_start_index;

		UINT num_boundary_verts;
		UINT num_boundary_faces;
		UINT boundary_start_index;
	};

	// Constant buffer
	struct Const_Per_Call
	{
		D3DXMATRIX	g_matLocal;
		D3DXMATRIX	g_matWorldViewProj;
		D3DXVECTOR2 g_UVBase;
		D3DXVECTOR2 g_PerlinMovement;
		D3DXVECTOR3	g_LocalEye;
	};

	struct Const_Shading
	{
		// Water-reflected sky color
		D3DXVECTOR3		g_SkyColor;
		float			unused0;
		// The color of bottomless water body
		D3DXVECTOR3		g_WaterbodyColor;

		// The strength, direction and color of sun streak
		float			g_Shineness;
		D3DXVECTOR3		g_SunDir;
		float			unused1;
		D3DXVECTOR3		g_SunColor;
		float			unused2;

		// The parameter is used for fixing an artifact
		D3DXVECTOR3		g_BendParam;

		// Perlin noise for distant wave crest
		float			g_PerlinSize;
		D3DXVECTOR3		g_PerlinAmplitude;
		float			unused3;
		D3DXVECTOR3		g_PerlinOctave;
		float			unused4;
		D3DXVECTOR3		g_PerlinGradient;

		// Constants for calculating texcoord from position
		float			g_TexelLength_x2;
		float			g_UVScale;
		float			g_UVOffset;
	};

public:
	DMOcean( DMObject* );
	~DMOcean();

	

	bool Initialize( DMD3D*, const OceanParameter& );
	void Shutdown( );
	void Render( DMCamera*, float time, ID3D11ShaderResourceView* displacemnet_map, ID3D11ShaderResourceView* gradient_map );

	int GetDrawCount( );

	bool GetHeightAtPosition( float, float, float& );
	int drawCalls();

	float terrainMultipler();

	

private:
	void CalculateMeshDimensions( int, float&, float&, float& );		
	int CountTriangles( float, float, float );
	bool IsTriangleContained( int, float, float, float );
	float estimateGridCoverage( const QuadNode& quad_node, DMCamera& camera, float screen_area );
	int buildNodeList( QuadNode& quad_node, DMCamera& camera );
	bool isLeaf( const QuadNode& quad_node );
	QuadRenderParam& selectMeshPattern( const QuadNode& quad_node );
	int searchLeaf( const std::vector<QuadNode>& node_list, const D3DXVECTOR2& point );
	void createSurfaceMesh( );
	int generateInnerMesh( RECT vert_rect, DWORD* output );
	int generateBoundaryMesh( int left_degree, int right_degree, int bottom_degree, int top_degree,
							  RECT vert_rect, DWORD* output );
	void createFresnelMap();
	void loadTextures();
	HRESULT CompileShaderFromFile( WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut );

private:
	std::vector<QuadNode> m_quad_node_list;
	float g_UpperGridCoverage;	
	// Quad-tree LOD, 0 to 9 (1x1 ~ 512x512) 	
	int g_Lods;
	int g_FurthestCover;
	int g_MeshDim;
	int g_PatchLength;
	QuadRenderParam g_mesh_patterns[9][3][3][3][3];
	// D3D11 buffers and layout
	ID3D11Buffer* g_pMeshVB;
	ID3D11Buffer* g_pMeshIB;
	ID3D11Buffer* g_pPerCallCB;
	ID3D11InputLayout* g_pMeshLayout = NULL;
	DMD3D* m_dmd3d;


	// Color look up 1D texture
	ID3D11Texture1D* g_pFresnelMap = NULL;
	ID3D11ShaderResourceView* g_pSRV_Fresnel = NULL;

	// Distant perlin wave
	DMTexture* g_pSRV_Perlin = NULL;

	// Environment maps
	DMTexture* g_pSRV_ReflectCube = NULL;

	// HLSL shaders
	ID3D11VertexShader* g_pOceanSurfVS = NULL;
	ID3D11PixelShader* g_pOceanSurfPS = NULL;
	ID3D11PixelShader* g_pWireframePS = NULL;

	// Samplers
	ID3D11SamplerState* g_pHeightSampler = NULL;
	ID3D11SamplerState* g_pGradientSampler = NULL;
	ID3D11SamplerState* g_pFresnelSampler = NULL;
	ID3D11SamplerState* g_pPerlinSampler = NULL;
	ID3D11SamplerState* g_pCubeSampler = NULL;


	// Dimension of displacement map
	int g_DisplaceMapDim;

	// State blocks
	ID3D11RasterizerState* g_pRSState_Solid = NULL;
	ID3D11RasterizerState* g_pRSState_Wireframe = NULL;
	ID3D11DepthStencilState* g_pDSState_Disable = NULL;
	ID3D11BlendState* g_pBState_Transparent = NULL;
	ID3D11Buffer* g_pPerFrameCB = NULL;
	ID3D11Buffer* g_pShadingCB = NULL;
};

