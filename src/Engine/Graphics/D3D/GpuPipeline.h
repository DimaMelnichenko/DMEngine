#pragma once

#include <cstdint>
#include <cstring>
#include "GpuResources.h"
#include "RenderState.h"

// Форматы целей прохода — часть PSO D3D12: одна фаза материала в проходе сцены (HDR + глубина), в каскаде теней
// (только глубина) и в тонмаппинге (задний буфер) — три пайплайна. В кадре их даёт текущий проход (DMD3D::passFormats
// по объявлению beginPass), при прогреве — тот, кто знает свои цели (SceneTargets::formats, DMD3D::backBufferFormats,
// TargetFormats::colorTarget)
struct TargetFormats
{
	static constexpr uint32_t maxColors = 8;

	DXGI_FORMAT color[maxColors] = {};
	uint32_t colorCount = 0;
	DXGI_FORMAT depth = DXGI_FORMAT_UNKNOWN;

	static TargetFormats colorTarget( DXGI_FORMAT colorFormat, DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN )
	{
		TargetFormats formats;
		formats.color[0] = colorFormat;
		formats.colorCount = 1;
		formats.depth = depthFormat;
		return formats;
	}
	static TargetFormats depthTarget( DXGI_FORMAT depthFormat )
	{
		TargetFormats formats;
		formats.depth = depthFormat;
		return formats;
	}
	bool operator==( const TargetFormats& other ) const
	{
		if( colorCount != other.colorCount || depth != other.depth )
			return false;
		for( uint32_t i = 0; i < colorCount; ++i )
			if( color[i] != other.color[i] )
				return false;
		return true;
	}
};

// Описание пайплайна GPU (не путать с Graphics/Pipeline.h — общие константы шейдеров): всё, что входит в PSO, — шейдеры
// стадий, раскладка вершин, состояния растеризатора, глубины и блендинга, топология, форматы целей. Собирается
// DMShader::setPass из фазы, текущего состояния (DMD3D::renderState) и целей текущего прохода и ставится в командный
// список одним DMD3D::setPipeline. Кэш — DMD3D::pipeline по ключу; набор нужных пайплайнов известен заранее и
// собирается при загрузке уровня (warmPipelines), собранный «лениво» в кадре — фриз, он считается и пишется в лог.
// Кэш на диске — ID3D12PipelineLibrary (cache/pipelines.bin), имя пайплайна — хэш содержимого описания
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
	TargetFormats formats;

	// Биты смещения глубины — в ключ и имя пайплайна (библиотека на диске)
	static uint64_t depthBiasBits( const DepthBias& bias )
	{
		uint32_t slope = 0, clamp = 0;
		memcpy( &slope, &bias.slopeScaled, sizeof( slope ) );
		memcpy( &clamp, &bias.clamp, sizeof( clamp ) );
		return static_cast<uint64_t>( slope ) | static_cast<uint64_t>( clamp ) << 32;
	}

	// Ключ кэша в памяти: FNV-1a по указателям стадий и раскладки (они живут, пока жив шейдер), состояниям и форматам
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
		mix( depthBiasBits( state.depthBias ) );
		for( uint32_t i = 0; i < formats.colorCount; ++i )
			mix( static_cast<uint64_t>( formats.color[i] ) | static_cast<uint64_t>( i ) << 32 );
		mix( static_cast<uint64_t>( formats.depth ) | static_cast<uint64_t>( formats.colorCount ) << 32 );
		return hash;
	}
};

class Pipeline
{
public:
	Pipeline( const PipelineDesc& desc, uint32_t id ) : m_desc( desc ), m_id( id ) {}

	const PipelineDesc& desc() const { return m_desc; }
	uint32_t id() const { return m_id; }
	// Объект состояния D3D12 (nullptr — собрать не удалось, вызовы с ним пропускаются)
	ID3D12PipelineState* object() const { return m_object.get(); }
	void setObject( ID3D12PipelineState* object ) { m_object.reset( object ); }

private:
	PipelineDesc m_desc;
	uint32_t m_id;
	com_unique_ptr<ID3D12PipelineState> m_object;
};
