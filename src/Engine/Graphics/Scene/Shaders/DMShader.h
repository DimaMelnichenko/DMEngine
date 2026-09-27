#pragma once

#include "DirectX.h"
#include <fstream>
#include <list>
#include <memory>
#include <optional>
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
	// argsOffset — смещение аргументов DrawIndexedInstancedIndirect в буфере, байты
	void renderInstancedIndirect( ID3D11Buffer* args, uint32_t argsOffset = 0 );
	bool setPass( int phase );
	void setLayoutDesc( std::vector<D3D11_INPUT_ELEMENT_DESC>&& vertex_layout );
	virtual void setParams( const PropertyContainer& );
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();

	// Режим и двусторонность материала с этими параметрами (Blend Mode и Two Sided в UE): по ним объект выбирает
	// проход и отсечение граней. По умолчанию — непрозрачный односторонний
	virtual MaterialRenderState renderState( const PropertyContainer& params ) const { return {}; }
	// Фаза (набор шейдеров) для этих параметров — например, вариант с отсечением по альфе; instanced — вариант
	// вершинного шейдера с матрицами экземпляров из буфера (INST_MATRIX); maskedInDepthPrepass — отсечение по альфе уже
	// сделал depth prepass, проход цвета с проверкой глубины EQUAL берёт вариант без clip (как
	// r.EarlyZPassOnlyMaterialMasking в UE). Рисуют так: setPass( phaseFor( params ) ), затем setParams( params )
	virtual int phaseFor( const PropertyContainer& params, bool instanced = false, bool maskedInDepthPrepass = false ) const
	{
		return 0;
	}
	// Есть ли вариант для инстансинга моделей: иначе одинаковые меши рисуются по одному
	virtual bool supportsInstancing() const { return false; }
	// Фаза «только глубина» для теней и depth prepass (без пиксельного шейдера или только с отсечением по альфе) или −1:
	// материал тень не отбрасывает и в prepass не рисуется
	virtual int depthPhaseFor( const PropertyContainer& params, bool instanced = false ) const { return -1; }

public:
	enum DrawType
	{
		skip, by_vertex, by_index, by_index_instance, by_auto
	};

	void setDrawType( DrawType );
	bool addShaderPassFromFile( SRVType type, const std::string& funcName, const std::string& fileName, const std::string& defines = "" );

	// Фаза — набор шейдеров стадий (номера в порядке addShaderPassFromFile, −1 — стадии нет). Возвращает номер фазы
	// для setPass: новой или уже существующей с теми же шейдерами; −1 — такого шейдера нет. Номер надо хранить:
	// у совпавшей фазы он не следующий по порядку
	int createPhase( int index_vs, int index_ps, int index_gs = -1, int index_hs = -1, int index_ds = -1 );
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
	// Копия исходника первого шейдера стадии type (не ссылка: addShaderPassFromFile дополняет список исходников)
	std::optional<ShaderSource> shaderSource( SRVType type ) const;

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