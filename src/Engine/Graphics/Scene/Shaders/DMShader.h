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
#include "Storage\DMResource.h"
#include "Properties/PropertyContainer.h"

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
	bool render( int indexCount, uint32_t vertexOffset = 0, uint32_t indexOffset = 0 );
	bool renderInstanced( int indexCount, uint32_t vertexOffset, uint32_t indexOffset, int instance_count );
	void renderInstancedIndirect( ID3D11Buffer* args );
	bool setPass( int phase );
	void setLayoutDesc( std::vector<D3D11_INPUT_ELEMENT_DESC>&& vertex_layout );
	virtual void setParams( const PropertyContainer& );
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();

public:
	enum DrawType
	{
		skip, by_vertex, by_index, by_index_instance, by_auto
	};

	void setDrawType( DrawType );
	bool addShaderPassFromFile( SRVType type, const std::string& funcName, const std::string& fileName, const std::string& defines = "" );
	bool addShaderPassFromMem( SRVType type, const std::string& funcName, const std::string& shaderCode, const std::string& defines = "" );

	bool createPhase( int index_vs, int index_ps, int index_gs = -1, int index_hs = -1, int index_ds = -1 );
	bool selectPhase( unsigned int idx );
	int phase();

private:

	struct Phase
	{
		int index_vs;
		int index_ps;
		int index_gs;
		int index_hs;
		int index_ds;

		bool operator==( const Phase& obj )
		{
			return this->index_vs == obj.index_vs &&
				this->index_gs == obj.index_gs &&
				this->index_ps == obj.index_ps &&
				this->index_hs == obj.index_hs &&
				this->index_ds == obj.index_ds;
		}
	};

	std::vector<Phase> m_phases;

private:
	virtual bool innerInitialize();
	virtual bool prepare();
	void OutputShaderErrorMessage( com_unique_ptr<ID3DBlob>&, const std::string& );
	void RenderShader( int, uint32_t vertexOffset, uint32_t indexOffset, int instanceCount = 0 );
	void parseDefines( std::string defines, std::vector<D3D_SHADER_MACRO>& macros );
	std::string version( SRVType type );

	bool createShaderPass( SRVType type, com_unique_ptr<ID3DBlob>& shaderBuffer );

private:
	std::vector<com_unique_ptr<ID3D11VertexShader>> m_vertexShader;
	std::vector<com_unique_ptr<ID3D11PixelShader>> m_pixelShader;
	std::vector<com_unique_ptr<ID3D11GeometryShader>> m_geometryShader;
	std::vector<com_unique_ptr<ID3D11HullShader>> m_hullShader;
	std::vector<com_unique_ptr<ID3D11DomainShader>> m_domainShader;
	com_unique_ptr<ID3D11InputLayout> m_layout;
	std::vector<D3D11_INPUT_ELEMENT_DESC> m_layoutDesc;
	DrawType m_drawType;
	int m_phaseIdx;
};

}