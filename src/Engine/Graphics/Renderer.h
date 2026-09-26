#pragma once

#include <chrono>
#include <string>
#include <vector>
#include "Scene\VertexPool.h"
#include "D3D\DMSamplerState.h"
#include "SceneObject.h"
#include "MeshBatch.h"
#include "PostProcess.h"
#include "D3D\GpuProfiler.h"
#include "D3D\DMStructuredBuffer.h"

class GUI;

namespace GS
{

class Scene;

// Отправляет на GPU кадр сцены: общие данные конвейера, compute-проходы объектов, сбор мешей с вида
// (MeshBatch — меш с материалом и матрицей, CustomBatch — свой вызов объекта), раскладка по проходам с сортировкой
// и проходы opaque → sky → transparent в HDR-буфер, затем постобработка (экспозиция, тонмаппинг) в задний буфер.
// Проходами и видами владеет рендерер, как mesh draw commands в UE: объекты не знают, в каком проходе рисуются их меши.
// Время CPU и GPU каждого объекта и прохода выводится в GUI, проходы подписаны метками событий для RenderDoc / PIX
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
	struct DrawCommand;

	void preparePipeline( Scene& scene, const FrameContext& frame );
	// Меши и свои вызовы видимых объектов с вида
	void collect( Scene& scene, const RenderView& view );
	// Раскладка собранного по проходам: меши — по режиму материала, свои вызовы — по маске; сортировка
	void buildCommands();
	void executePass( MeshPass pass, const RenderView& view, RasterState frameRaster );
	// Единственное место, где рисуется MeshBatch: вариант шейдера, параметры, растеризатор, матрица, вызов
	void drawMesh( const MeshBatch& batch, const RenderContext& context );
	// Команды first…last — один меш с одними параметрами: одним DrawIndexedInstanced с матрицами экземпляров в буфере
	void drawMeshInstanced( const std::vector<DrawCommand>& commands, size_t first, size_t last,
							const RenderContext& context );

	// Время CPU на отправку команд и область GPU-профайлера с тем же именем
	template<typename Func>
	void measure( const std::string& counterName, Func&& func );
	void reportGpuTimes();

private:
	GUI& m_gui;
	VertexPool m_vertexPool;

	// Команда прохода — батч и ключ сортировки (как FMeshDrawCommand в UE). Непрозрачные: объекты в порядке сцены,
	// внутри объекта — по материалу, варианту шейдера, растеризатору и мешу; прозрачные — от дальних к ближним
	struct DrawCommand
	{
		uint64_t key;
		uint32_t index;		// в meshes() или customs() сборщика
		bool custom;
	};
	MeshCollector m_collector;
	std::vector<DrawCommand> m_commands[meshPassCount];

	// Матрицы экземпляров инстансного вызова; раскладка — InstanceTransform в Shaders/instance.sh
	struct InstanceTransform
	{
		XMMATRIX world;
		XMMATRIX worldInverseTranspose;
	};
	static constexpr uint32_t maxInstancesPerDraw = 1024;
	DMStructuredBuffer m_instanceBuffer;
	std::vector<InstanceTransform> m_instanceTransforms;
	// За кадр: сколько мешей нарисовано и сколькими вызовами — видно, работает ли инстансинг
	uint32_t m_meshCount = 0;
	uint32_t m_meshDraws = 0;
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
