#pragma once

#include <vector>
#include "SceneObject.h"
#include "ShaderProgram.h"
#include "Level\LevelSettings.h"

namespace GS
{

// Ручьи — ленты воды вдоль осей (как реки-сплайны Water Body River в UE): точки из конвейера рельефа и воды
// (TerrainHydrology) — уровень воды по Маннингу, полуширина до края ложбины, скорость и пена. Лента плоская
// поперёк, её края уходят под берег — урез даёт проверка глубины. Строится один раз при загрузке, рисуется одним
// вызовом в проходе transparent с материалом воды (Shaders/stream_water.vs/.ps, Shaders/water_shading.sh). Константы
// материала (b2) и шум (t1) ставит WaterSimulation перед вызовом
class StreamRibbons
{
public:
	bool initialize( const std::vector<WaterStream>& streams );
	bool empty() const { return m_indexCount == 0; }
	void warmPipelines( const PassStates& states );
	void render( const RenderContext& context );

private:
	// Раскладка — VertexInputType в Shaders/stream_water.vs
	struct Vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 flow;		// скорость X, Z, м/с; пена; поперёк ленты −1…1
	};

	ShaderProgram m_program;
	int m_phase = -1;
	Buffer m_vertexBuffer;
	Buffer m_indexBuffer;
	uint32_t m_indexCount = 0;
};

}
