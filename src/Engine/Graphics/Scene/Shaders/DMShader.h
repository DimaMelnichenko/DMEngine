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
#include "MaterialRenderState.h"

namespace GS
{

class DMShader
{
public:
	DMShader();
	virtual ~DMShader();
	bool initialize();
	bool render( int indexCount, uint32_t vertexOffset = 0, uint32_t indexOffset = 0 );
	bool renderInstanced( int indexCount, uint32_t vertexOffset, uint32_t indexOffset, int instance_count );
	void renderInstancedIndirect( ID3D11Buffer* args );
	bool setPass( int phase );
	void setLayoutDesc( std::vector<D3D11_INPUT_ELEMENT_DESC>&& vertex_layout );
	virtual void setParams( const PropertyContainer& );
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();

	// Режим и двусторонность материала с этими параметрами (Blend Mode и Two Sided в UE): по ним объект выбирает
	// проход и отсечение граней. По умолчанию — непрозрачный односторонний
	virtual MaterialRenderState renderState( const PropertyContainer& params ) const { return {}; }
	// Фаза (набор шейдеров) для этих параметров — например, вариант с отсечением по альфе; instanced — вариант
	// вершинного шейдера с матрицами экземпляров из буфера (INST_MATRIX). Рисуют так:
	// setPass( phaseFor( params ) ), затем setParams( params )
	virtual int phaseFor( const PropertyContainer& params, bool instanced = false ) const { return 0; }
	// Есть ли вариант для инстансинга моделей: иначе одинаковые меши рисуются по одному
	virtual bool supportsInstancing() const { return false; }

public:
	enum DrawType
	{
		skip, by_vertex, by_index, by_index_instance, by_auto
	};

	void setDrawType( DrawType );
	bool addShaderPassFromFile( SRVType type, const std::string& funcName, const std::string& fileName, const std::string& defines = "" );

	bool createPhase( int index_vs, int index_ps, int index_gs = -1, int index_hs = -1, int index_ds = -1 );
	bool selectPhase( unsigned int idx );
	int phase();

protected:
	// Исходник скомпилированного шейдера: материал может собрать из него свой вариант с другими defines
	struct ShaderSource
	{
		SRVType type;
		std::string function;
		std::string file;
		std::string defines;
	};
	// Первый шейдер стадии type или nullptr
	const ShaderSource* shaderSource( SRVType type ) const;

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
	std::vector<ShaderSource> m_sources;
	com_unique_ptr<ID3D11InputLayout> m_layout;
	std::vector<D3D11_INPUT_ELEMENT_DESC> m_layoutDesc;
	DrawType m_drawType;
	int m_phaseIdx;
};

}