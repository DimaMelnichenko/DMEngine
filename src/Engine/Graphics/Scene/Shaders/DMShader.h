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
	void renderInstancedIndirect( const Buffer& args, uint32_t argsOffset = 0 );
	// Ставит пайплайн фазы: её шейдеры, раскладка вершин, топология и текущие состояния DMD3D (setState / ScopedRenderState
	// — до setPass). Пайплайн — из кэша DMD3D::pipeline
	bool setPass( int phase );
	// Топология вершин пайплайна (частицы — точки); по умолчанию треугольники
	void setTopology( D3D_PRIMITIVE_TOPOLOGY topology ) { m_topology = topology; }
	int phaseCount() const { return static_cast<int>( m_phases.size() ); }
	// Описание пайплайна фазы для состояния state и целей formats (без привязки)
	PipelineDesc pipelineDesc( int phase, const RenderState& state, const TargetFormats& formats ) const;
	// Собрать пайплайны всех фаз для этих состояний и целей при загрузке (рендерер знает состояния и цели своих
	// проходов): сборка PSO в кадре — фриз, собранный в кадре считается «ленивым» и пишется в лог
	void warmPipelines( const std::vector<RenderState>& states, const TargetFormats& formats );
	// То же для части фаз: цветные фазы — с целями сцены, фазы «только глубина» — без цели цвета (пиксельный шейдер с
	// SV_Target без цели — предупреждение debug-слоя и лишний PSO)
	void warmPipelines( const std::vector<RenderState>& states, const TargetFormats& formats, const std::vector<int>& phases );
	// Фазы «только глубина» (тени, depth prepass): по умолчанию — без пиксельного шейдера; материал с пиксельным шейдером
	// отсечения (mainDepth) перечисляет свои сам
	virtual std::vector<int> depthPhases() const;
	// Остальные фазы — рисуют цвет
	std::vector<int> colorPhases() const;
	void setLayoutDesc( std::vector<VertexElement>&& vertex_layout );
	virtual void setParams( const PropertyContainer& );
	virtual std::vector<VertexElement> initLayouts();

	// Режим и двусторонность материала с этими параметрами (Blend Mode и Two Sided в UE): по ним объект выбирает
	// проход и отсечение граней. По умолчанию — непрозрачный односторонний
	virtual MaterialRenderState renderState( const PropertyContainer& params ) const { return {}; }
	// Фаза (набор шейдеров) для этих параметров — например, вариант с отсечением по альфе — и вызова (options: инстансы,
	// глубина из depth prepass, смена LOD дизерингом). Рисуют так: setPass( phaseFor( params ) ), затем setParams( params )
	virtual int phaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const
	{
		return 0;
	}
	// Есть ли вариант для инстансинга моделей: иначе одинаковые меши рисуются по одному
	virtual bool supportsInstancing() const { return false; }
	// Фаза «только глубина» для теней и depth prepass (без пиксельного шейдера или только с отсечением — по альфе,
	// дизерингом смены LOD) или −1: материал тень не отбрасывает и в prepass не рисуется. Из options важны instanced
	// и lodDither
	virtual int depthPhaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const { return -1; }

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
	void RenderShader( int, uint32_t vertexOffset, uint32_t indexOffset, int instanceCount = 0 );

	bool createShaderPass( SRVType type, const std::vector<uint8_t>& bytecode );
	// Стадия фазы: шейдер по номеру в списке стадии или nullptr — стадия выключена
	const ShaderStage* stage( const std::vector<ShaderStage>& stages, int index ) const
	{
		return index >= 0 && index < static_cast<int>( stages.size() ) ? &stages[index] : nullptr;
	}

private:
	// Скомпилированные стадии по типу; номера в фазах — индексы в этих списках
	std::vector<ShaderStage> m_vertexShader;
	std::vector<ShaderStage> m_pixelShader;
	std::vector<ShaderStage> m_geometryShader;
	std::vector<ShaderStage> m_hullShader;
	std::vector<ShaderStage> m_domainShader;
	std::vector<ShaderSource> m_sources;
	InputLayout m_layout;
	std::vector<VertexElement> m_layoutDesc;
	D3D_PRIMITIVE_TOPOLOGY m_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	DrawType m_drawType;
	int m_phaseIdx;
};

}
