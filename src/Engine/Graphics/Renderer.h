#pragma once

#include <chrono>
#include <string>
#include <vector>
#include "Scene\VertexPool.h"
#include "D3D\DMSamplerState.h"
#include "SceneObject.h"
#include "PostProcess.h"
#include "D3D\GpuProfiler.h"

class GUI;

namespace GS
{

class Scene;

// Отправляет на GPU кадр сцены: общие данные конвейера, compute-проходы объектов,
// затем проходы отрисовки sky → opaque → transparent в HDR-буфер и постобработку (экспозиция, тонмаппинг)
// в задний буфер. Время CPU и GPU каждого объекта и прохода выводится в GUI, проходы подписаны метками событий
// для RenderDoc / PIX
class Renderer
{
public:
	explicit Renderer( GUI& gui );

	// Вызывается после загрузки мешей: собирает общий буфер вершин и индексов
	bool initialize();

	// Рисует сцену в HDR-буфер и тонмаппинг в задний буфер; дальше DMGraphics рисует GUI и вызывает EndScene
	void render( Scene& scene, const FrameContext& frame, bool wireframe );
	// Свойства постобработки для GUI
	PropertyContainer* postProcessProperties();

private:
	void preparePipeline( Scene& scene, const FrameContext& frame );
	void renderPass( Scene& scene, const FrameContext& frame, RenderPass pass );

	// Время CPU на отправку команд и область GPU-профайлера с тем же именем
	template<typename Func>
	void measure( const std::string& counterName, Func&& func );
	void reportGpuTimes();

private:
	GUI& m_gui;
	VertexPool m_vertexPool;
	DMSamplerState m_samplerState;
	PostProcess m_postProcess;
	GpuProfiler m_gpuProfiler;

	// Среднее время GPU за первые секунды после прогрева — одной строкой в log.txt («GPU average»): её печатает
	// Tools/run.ps1, чтобы сравнивать производительность до и после изменений
	struct GpuAverage
	{
		std::string name;
		double sum = 0.0;
		uint32_t count = 0;
	};
	std::vector<GpuAverage> m_gpuAverages;	// первый элемент — весь кадр
	uint32_t m_frameIndex = 0;
	std::chrono::steady_clock::time_point m_gpuAverageStart;
	bool m_gpuAverageLogged = false;
};

}
