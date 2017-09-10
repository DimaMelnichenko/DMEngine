#pragma once

#include <d3d11.h>
#include <d3dx10math.h>
#include <d3dx11async.h>
#include <fstream>
#include <list>
#include <memory>
#include <vector>

#include "..\..\Common\DMObject.h"
#include "..\Camera\DMCamera.h"
#include "..\..\D3D\DMD3D.h"

class DMShader : public DMObject
{
	struct alignas(16) VPCameraBuffer
	{		
		D3DXMATRIX view;
		D3DXMATRIX projection;
		D3DXVECTOR3 camera_position;
		D3DXVECTOR3 view_direction;
	};

	struct alignas(16) WorldBuffer
	{
		D3DXMATRIX world;
	};

	struct alignas(16) PSApplication
	{		
		D3DXVECTOR3 camera_position;
		float app_time;
		float elapsed_time;
	};

public:
	DMShader( DMD3D* );
	virtual ~DMShader();
	
	bool Initialize( WCHAR* vsFilename, WCHAR* psFilename, bool use_strimout_gs = false );
	bool Initialize( WCHAR* vsFilename, bool use_strimout_gs = false );
	bool Initialize();
	void setStreamout( bool use_strimout_gs );
	bool Render( int indexCount, D3DXMATRIX* worldMatrix );
	bool RenderInstanced( int indexCount, int instance_count, D3DXMATRIX* worldMatrix );
	bool Prepare( DMCamera*, int phase );
	virtual void Shutdown( );
	virtual void Update( float ) = 0;

public:
	enum DrawType
	{
		skip, by_vertex, by_index, by_index_instance, by_auto
	};

	enum ShaderType
	{
		vs, ps, gs
	};

	
	void setDrawType( DrawType );
	bool addShaderPass( ShaderType, const char* function_name, const WCHAR* file_name, const std::string& defines = "" );	

	void createPhase( int index_vs, int index_gs, int index_ps );
	bool selectPhase( unsigned int idx );
	int phase();

private:

	struct Phase
	{
		int index_vs;
		int index_ps;
		int index_gs;
		

		bool operator==( const Phase& obj )
		{
			return this->index_vs == obj.index_vs &&
				this->index_gs == obj.index_gs &&
				this->index_ps == obj.index_ps;
		}
	};

	std::vector<Phase> m_phases;

private:
	virtual void prepareRender() = 0;
	bool initialize();
	virtual bool prepare();
	void OutputShaderErrorMessage( ID3D10Blob*, const WCHAR* );
	void RenderShader( int, int = 0 );
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts() = 0;
	void setCamera( DMCamera* camera );
	void setWorldMatrix( D3DXMATRIX* worldMatrix );	
	virtual void StrimOutputDeclaration( D3D11_SO_DECLARATION_ENTRY* );
	void parse_defines( std::string, D3D10_SHADER_MACRO** );

protected:
	DMD3D* m_dmd3d;	


private:
	std::vector<com_unique_ptr<ID3D11VertexShader>> m_vertex_shader;
	std::vector<com_unique_ptr<ID3D11PixelShader>> m_pixel_shader;
	std::vector<com_unique_ptr<ID3D11GeometryShader>> m_geometry_shader;
	std::vector<com_unique_ptr<ID3D11InputLayout>> m_layout;
	com_unique_ptr<ID3D11Buffer> m_vpcamera_buffer;
	com_unique_ptr<ID3D11Buffer> m_world_buffer;
	com_unique_ptr<ID3D11Buffer> m_ps_application_buffer;
	DrawType m_draw_type;
	int m_phase_idx;
	bool m_use_strimout_gs;	
	DMTimer* m_timer;
};

