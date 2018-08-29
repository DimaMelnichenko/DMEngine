#pragma once

#include "DirectX.h"
#include <fstream>
#include <list>
#include <memory>
#include <vector>

#include "Utils/DMTimer.h"
#include "Camera\DMCamera.h"
#include "D3D\DMD3D.h"
#include "DM3DUtils.h"
#include "DMResource.h"
#include "Materials\Parameter\Parameter.h"

namespace GS
{

class DMShader
{
public:
	DMShader();
	virtual ~DMShader();

	bool initialize( const std::string& vsFilename, const std::string& psFilename, bool use_strimout_gs = false );
	bool initialize( const std::string& vsFilename, bool use_strimout_gs = false );
	bool initialize();
	void setStreamout( bool use_strimout_gs );
	bool render( int indexCount, uint32_t vertexOffset, uint32_t indexOffset );
	bool renderInstanced( int indexCount, uint32_t vertexOffset, uint32_t indexOffset, int instance_count );
	void renderInstancedIndirect( ID3D11Buffer* args );
	bool setPass( int phase );
	virtual void setParams( const ParamSet& ) = 0;

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
	void parseDefines( std::string, D3D10_SHADER_MACRO** );

private:
	std::vector<com_unique_ptr<ID3D11VertexShader>> m_vertexShader;
	std::vector<com_unique_ptr<ID3D11PixelShader>> m_pixelShader;
	std::vector<com_unique_ptr<ID3D11GeometryShader>> m_geometryShader;
	std::vector<com_unique_ptr<ID3D11InputLayout>> m_layout;
	DrawType m_drawType;
	int m_phaseIdx;
	bool m_useStrimoutGS;
};

}