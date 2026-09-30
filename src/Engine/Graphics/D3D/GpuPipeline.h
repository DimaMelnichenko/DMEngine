#pragma once

#include <cstdint>
#include "GpuResources.h"
#include "RenderState.h"

// Объект пайплайна GPU (шаг A4 плана переезда на D3D12; не путать с Graphics/Pipeline.h — общие константы шейдеров): всё, что в D3D12 входит в PSO, — шейдеры стадий, раскладка вершин,
// состояния растеризатора, глубины и блендинга, топология, форматы целей. Собирается DMShader::setPass из фазы и
// текущего состояния (DMD3D::renderState) и ставится в контекст одним DMD3D::setPipeline. Кэш — DMD3D::pipeline по
// ключу; набор нужных пайплайнов известен заранее и собирается при загрузке уровня (warmPipelines), собранный «лениво»
// в кадре считается и пишется в лог — в D3D12 это был бы фриз
struct PipelineDesc
{
	const ShaderStage* vertex = nullptr;
	const ShaderStage* pixel = nullptr;
	const ShaderStage* geometry = nullptr;
	const ShaderStage* hull = nullptr;
	const ShaderStage* domain = nullptr;
	const InputLayout* layout = nullptr;
	RenderState state;
	D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// Форматы целей — часть PSO в D3D12; в D3D11 не нужны, задаст объявление прохода (шаг A5)
	DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;

	// Ключ кэша: FNV-1a по указателям стадий и раскладки (они живут, пока жив шейдер) и состояниям
	uint64_t key() const
	{
		uint64_t hash = 14695981039346656037ull;
		const auto mix = [&hash]( uint64_t value )
		{
			for( int i = 0; i < 8; ++i )
			{
				hash ^= ( value >> ( i * 8 ) ) & 0xFF;
				hash *= 1099511628211ull;
			}
		};
		mix( reinterpret_cast<uintptr_t>( vertex ) );
		mix( reinterpret_cast<uintptr_t>( pixel ) );
		mix( reinterpret_cast<uintptr_t>( geometry ) );
		mix( reinterpret_cast<uintptr_t>( hull ) );
		mix( reinterpret_cast<uintptr_t>( domain ) );
		mix( reinterpret_cast<uintptr_t>( layout ) );
		mix( static_cast<uint64_t>( state.raster ) | static_cast<uint64_t>( state.depth ) << 8 | static_cast<uint64_t>( state.blend ) << 16 |
			 static_cast<uint64_t>( topology ) << 24 );
		mix( static_cast<uint64_t>( colorFormat ) | static_cast<uint64_t>( depthFormat ) << 32 );
		return hash;
	}
};

class Pipeline
{
public:
	Pipeline( const PipelineDesc& desc, uint32_t id ) : m_desc( desc ), m_id( id ) {}

	const PipelineDesc& desc() const { return m_desc; }
	uint32_t id() const { return m_id; }

private:
	PipelineDesc m_desc;
	uint32_t m_id;
};
