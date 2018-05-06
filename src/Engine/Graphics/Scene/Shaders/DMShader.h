#pragma once

#include "DirectX.h"
#include <fstream>
#include <list>
#include <memory>
#include <vector>

#include "Utils/DMTimer.h"
#include "..\Camera\DMCamera.h"
#include "..\..\D3D\DMD3D.h"
#include "DM3DUtils.h"
#include "DMResource.h"

namespace GS
{

class DMShader //: public DMResource
{
public:
	DMShader();
	virtual ~DMShader();

	bool Initialize( const std::string& vsFilename, const std::string& psFilename, bool use_strimout_gs = false );
	bool Initialize( const std::string& vsFilename, bool use_strimout_gs = false );
	bool Initialize();
	void setStreamout( bool use_strimout_gs );
	bool Render( int indexCount, uint32_t vertexOffset, uint32_t indexOffset );
	bool RenderInstanced( int indexCount, uint32_t vertexOffset, uint32_t indexOffset, int instance_count );
	bool setPass( int phase );

public:
	enum DrawType
	{
		skip, by_vertex, by_index, by_index_instance, by_auto
	};

	void setDrawType( DrawType );
	bool addShaderPass( SRVType type, const std::string& funcName, const std::string& fileName, const std::string& defines = "" );

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
	virtual bool innerInitialize() = 0;
	virtual bool prepare();
	void OutputShaderErrorMessage( ID3D10Blob*, const std::string& );
	void RenderShader( int, uint32_t vertexOffset, uint32_t indexOffset, int = 0 );
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts() = 0;
	virtual void StrimOutputDeclaration( D3D11_SO_DECLARATION_ENTRY* );
	void parse_defines( std::string, D3D10_SHADER_MACRO** );

protected:
	DMD3D* m_dmd3d;


private:
	std::vector<com_unique_ptr<ID3D11VertexShader>> m_vertex_shader;
	std::vector<com_unique_ptr<ID3D11PixelShader>> m_pixel_shader;
	std::vector<com_unique_ptr<ID3D11GeometryShader>> m_geometry_shader;
	std::vector<com_unique_ptr<ID3D11InputLayout>> m_layout;
	DrawType m_draw_type;
	int m_phase_idx;
	bool m_use_strimout_gs;
};

}