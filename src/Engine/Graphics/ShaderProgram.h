#pragma once

#include <optional>
#include <string>
#include <vector>

#include "D3D\DMD3D.h"

namespace GS
{

// Программа шейдеров: скомпилированные стадии, фазы — их наборы (варианты шейдера), раскладка вершин и топология.
// setPass ставит пайплайн фазы под текущие состояния и цели прохода; рисует тот, кто поставил фазу (DMD3D::draw*).
// Основа материалов (Material) и своих шейдеров объектов и проходов без материала (террейн, FullscreenShader)
class ShaderProgram
{
public:
	// Компиляция стадии из файла (DXC → DXIL SM 6.6 с кэшем на диске, D3D/ShaderCompiler.h; ошибки — в лог и
	// shader-error.txt). Номер шейдера в фазе (createPhase) — порядок добавления среди шейдеров этой стадии
	bool addShaderPassFromFile( ShaderStageType type, const std::string& funcName, const std::string& fileName, const std::string& defines = "" );
	// Фаза — набор шейдеров стадий (номера в порядке addShaderPassFromFile, −1 — стадии нет). Возвращает номер фазы
	// для setPass: новой или уже существующей с теми же шейдерами; −1 — такого шейдера нет. Номер надо хранить:
	// у совпавшей фазы он не следующий по порядку
	int createPhase( int index_vs, int index_ps, int index_gs = -1 );
	int phaseCount() const { return static_cast<int>( m_phases.size() ); }
	// Фаза без пиксельного шейдера — «только глубина» (тени, depth prepass)
	bool hasPixelShader( int phase ) const { return m_phases[phase].index_ps >= 0; }

	// Раскладка вершин — до первого вершинного шейдера (она создаётся вместе с ним)
	void setLayoutDesc( std::vector<VertexElement>&& layoutDesc );
	// Топология вершин пайплайна (частицы — точки); по умолчанию треугольники
	void setTopology( D3D_PRIMITIVE_TOPOLOGY topology ) { m_topology = topology; }

	// Ставит пайплайн фазы: её шейдеры, раскладка вершин, топология и текущие состояния DMD3D (setState / ScopedRenderState
	// — до setPass). Пайплайн — из кэша DMD3D::pipeline; false — такой фазы нет
	bool setPass( int phase );
	// Описание пайплайна фазы для состояния state и целей formats (без привязки)
	PipelineDesc pipelineDesc( int phase, const RenderState& state, const TargetFormats& formats ) const;
	// Собрать пайплайны всех фаз для этих состояний и целей при загрузке (рендерер знает состояния и цели своих
	// проходов): сборка PSO в кадре — фриз, собранный в кадре считается «ленивым» и пишется в лог
	void warmPipelines( const std::vector<RenderState>& states, const TargetFormats& formats );
	// То же для части фаз: цветные фазы — с целями сцены, фазы «только глубина» — без цели цвета (пиксельный шейдер с
	// SV_Target без цели — предупреждение debug-слоя и лишний PSO)
	void warmPipelines( const std::vector<RenderState>& states, const TargetFormats& formats, const std::vector<int>& phases );

protected:
	// Исходник скомпилированного шейдера: материал может собрать из него свой вариант с другими defines
	struct ShaderSource
	{
		ShaderStageType type;
		std::string function;
		std::string file;
		std::string defines;
	};
	// Копия исходника первого шейдера стадии type (не ссылка: addShaderPassFromFile дополняет список исходников)
	std::optional<ShaderSource> shaderSource( ShaderStageType type ) const;

private:
	struct Phase
	{
		int index_vs;
		int index_ps;
		int index_gs;

		bool operator==( const Phase& other ) const
		{
			return index_vs == other.index_vs && index_ps == other.index_ps && index_gs == other.index_gs;
		}
	};

	bool createShaderPass( ShaderStageType type, const std::vector<uint8_t>& bytecode );
	// Стадия фазы: шейдер по номеру в списке стадии или nullptr — стадия выключена
	const ShaderStage* stage( const std::vector<ShaderStage>& stages, int index ) const
	{
		return index >= 0 && index < static_cast<int>( stages.size() ) ? &stages[index] : nullptr;
	}

	std::vector<Phase> m_phases;
	// Скомпилированные стадии по типу; номера в фазах — индексы в этих списках
	std::vector<ShaderStage> m_vertexShader;
	std::vector<ShaderStage> m_pixelShader;
	std::vector<ShaderStage> m_geometryShader;
	std::vector<ShaderSource> m_sources;
	InputLayout m_layout;
	std::vector<VertexElement> m_layoutDesc;
	D3D_PRIMITIVE_TOPOLOGY m_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

}
