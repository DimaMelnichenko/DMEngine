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
#include "ShadowCascades.h"

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
	// Уровень уже прочитан (Scene::loadResources): постобработка — его настройки; shadowResolution — размер карты теней
	bool initialize( const PostProcess::Settings& postProcess, uint32_t shadowResolution );

	// Рисует сцену в HDR-буфер и тонмаппинг в задний буфер; дальше DMGraphics рисует GUI и вызывает EndScene
	void render( Scene& scene, const FrameContext& frame, bool wireframe );
	// Свойства постобработки и теней для GUI
	PropertyContainer* postProcessProperties();
	PropertyContainer* shadowProperties();
	// Текущие настройки постобработки — для сохранения уровня
	PostProcess::Settings postProcessSettings();

private:
	struct DrawCommand;

	void preparePipeline( Scene& scene, const FrameContext& frame );
	// Меши и свои вызовы видимых объектов с вида
	void collect( Scene& scene, const RenderView& view, MeshCollector& collector );
	// Раскладка собранного с главного вида по проходам сцены: меши — по режиму материала, свои вызовы — по маске
	void buildCommands();
	// Раскладка для прохода глубины тени: меши, отбрасывающие тень (материал с вариантом «только глубина», не
	// полупрозрачные), и свои вызовы с битом прохода
	void buildShadowCommands();
	// Каскады теней солнца — до проходов сцены: на каждый каскад сбор с его вида и глубина в срез карты
	void renderShadows( Scene& scene, const FrameContext& frame );
	void executePass( MeshPass pass, const RenderView& view, RasterState frameRaster );
	// Команды прохода сериями по объектам; measureOwners — строка времени CPU и GPU на каждую серию
	void executeCommands( const MeshCollector& collector, const std::vector<DrawCommand>& commands,
						  const RenderContext& context, bool measureOwners );
	// Единственное место, где рисуется MeshBatch: вариант шейдера (в проходе теней — «только глубина»), параметры,
	// растеризатор, матрица, вызов
	void drawMesh( const MeshBatch& batch, const RenderContext& context );
	// Команды first…last — один меш с одними параметрами: одним DrawIndexedInstanced с матрицами экземпляров в буфере
	void drawMeshInstanced( const std::vector<MeshBatch>& meshes, const std::vector<DrawCommand>& commands,
							size_t first, size_t last, const RenderContext& context );

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
	std::vector<DrawCommand> m_commands[scenePassCount];
	// Проход теней: свой сборщик и команды на каждый каскад
	ShadowCascades m_shadows;
	MeshCollector m_shadowCollector;
	std::vector<DrawCommand> m_shadowCommands;

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
	uint32_t m_shadowMeshCount = 0;
	uint32_t m_shadowDraws = 0;
	float m_sunGroundIlluminance = 0.0f;	// освещённость от солнца у земли, лк — в «Statistic» и строку «GPU average»
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
