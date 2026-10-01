#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include "Scene\VertexPool.h"
#include "SceneObject.h"
#include "MeshBatch.h"
#include "PostProcess.h"
#include "D3D\GpuProfiler.h"
#include "D3D\DMStructuredBuffer.h"
#include "ShadowCascades.h"
#include "SceneTargets.h"
#include "ConstantBuffers.h"

class GUI;

namespace GS
{

class Scene;

// Отправляет на GPU кадр сцены: общие данные конвейера, compute-проходы объектов, сбор мешей с вида
// (MeshBatch — меш с материалом и матрицей, CustomBatch — свой вызов объекта), раскладка по проходам с сортировкой
// и проходы depth prepass → opaque → sky → transparent в HDR-буфер, затем постобработка (экспозиция, тонмаппинг) в
// задний буфер.
// Проходами и видами владеет рендерер, как mesh draw commands в UE: объекты не знают, в каком проходе рисуются их меши.
// Время CPU и GPU каждого объекта и прохода выводится в GUI, проходы подписаны метками событий для RenderDoc / PIX
class Renderer
{
public:
	explicit Renderer( GUI& gui );

	// Вызывается после загрузки мешей: собирает общий буфер вершин и индексов, создаёт буфер сцены (SceneTargets).
	// Уровень уже прочитан (Scene::loadResources): постобработка — его настройки; shadowResolution — размер карты теней,
	// depthPrepass — начальное значение флажка «Depth prepass» (DepthPrepass в settings.ini)
	bool initialize( const PostProcess::Settings& postProcess, uint32_t shadowResolution, bool depthPrepass );
	// Новый размер кадра (WM_SIZE): буфер сцены и цели постобработки заново; задний буфер уже пересоздал DMD3D::resize
	bool resize();
	// Пайплайны всех материалов и объектов сцены для состояний проходов кадра — после Scene::initialize при загрузке
	// уровня (GpuPipeline.h). Состояние прохода теней — со смещением глубины солнца уровня (ShadowCascades::renderState)
	void warmPipelines( Scene& scene );

	// Рисует сцену в HDR-буфер (SceneTargets) и тонмаппинг в задний буфер; дальше DMGraphics рисует GUI и вызывает endFrame
	void render( Scene& scene, const FrameContext& frame, bool wireframe );
	// Свойства постобработки и теней для GUI; properties — окно «Renderer»: проходы кадра («Depth prepass»)
	PropertyContainer* postProcessProperties();
	PropertyContainer* shadowProperties();
	PropertyContainer* properties();
	// Текущие настройки постобработки — для сохранения уровня
	PostProcess::Settings postProcessSettings();
	// Среднее время GPU кадра и проходов за seconds секунд с этого кадра — строкой, как «GPU average» в log.txt
	// (команда stat gpu удалённого управления)
	void measureGpu( float seconds, std::function<void( const std::string& )> done );
	// Смена плана: экспозиция адаптируется сразу (PostProcess::cameraCut)
	void cameraCut() { m_postProcess.cameraCut(); }

private:
	struct DrawCommand;

	void preparePipeline( Scene& scene, const FrameContext& frame );
	// Состояния и цели проходов для прогрева пайплайнов объектов (SceneObject::warmPipelines)
	PassStates passStates( const RenderState& shadowState ) const;
	// Пайплайны теней для нового состояния прохода теней (смещение сменилось в GUI, тени перешли с солнца на луну):
	// фазы «только глубина» материалов и объекты — до прохода, иначе они собрались бы в кадре «лениво»
	void warmShadowPipelines( Scene& scene );
	// Меши и свои вызовы видимых объектов с вида
	void collect( Scene& scene, const RenderView& view, MeshCollector& collector );
	// Раскладка собранного с главного вида по проходам сцены: меши — по режиму материала, свои вызовы — по маске;
	// с depthPrepass непрозрачные и Masked с вариантом «только глубина» — ещё и в depth prepass
	void buildCommands( bool depthPrepass );
	// Раскладка для прохода глубины тени: меши, отбрасывающие тень (материал с вариантом «только глубина», не
	// полупрозрачные), и свои вызовы с битом прохода
	void buildShadowCommands();
	// Каскады теней солнца — до проходов сцены: на каждый каскад сбор с его вида и глубина в срез карты
	void renderShadows( Scene& scene, const FrameContext& frame );
	// depthFromPrepass — глубина вида уже записана depth prepass (RenderContext::depthFromPrepass)
	void executePass( MeshPass pass, const RenderView& view, RasterState frameRaster, bool depthFromPrepass = false );
	// Команды прохода сериями по объектам; measureOwners — строка времени CPU и GPU на каждую серию
	void executeCommands( const MeshCollector& collector, const std::vector<DrawCommand>& commands,
						  const RenderContext& context, bool measureOwners );
	// Единственное место, где рисуется MeshBatch: вариант шейдера (в проходе теней — «только глубина»), параметры,
	// растеризатор, матрица, вызов
	void drawMesh( const MeshBatch& batch, const RenderContext& context );
	// Команды first…last — один меш с одними параметрами: одним DrawIndexedInstanced с матрицами экземпляров в буфере
	void drawMeshInstanced( const std::vector<MeshBatch>& meshes, const std::vector<DrawCommand>& commands,
							size_t first, size_t last, const RenderContext& context );
	// Счётчики мешей и вызовов кадра: проход теней — свои, depth prepass не считается
	void countMeshes( MeshPass pass, uint32_t meshes, uint32_t draws );

	// Время CPU на отправку команд и область GPU-профайлера с тем же именем
	template<typename Func>
	void measure( const std::string& counterName, Func&& func );
	void reportGpuTimes();

private:
	GUI& m_gui;
	VertexPool m_vertexPool;
	// Константы кадра и вида (b0) и объекта (b1); своим вызовам объектов приходят в RenderContext::constants
	ConstantBuffers m_constants;
	PropertyContainer m_properties;
	// Буфер сцены (HDR-цвет и глубина) — цели проходов кадра; цвет очистки — почти чёрный линейный (небо рисует фон там,
	// где сцена ничего не нарисовала)
	SceneTargets m_sceneTargets;
	static constexpr float sceneClearColor[4] = { 0.004f, 0.004f, 0.004f, 1.0f };

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
	bool m_shadowsActive = false;	// каскады этого кадра посчитаны (update перед compute объектов)
	RenderState m_warmedShadowState;	// состояние прохода теней, для которого прогреты пайплайны (warmShadowPipelines)
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
	PostProcess m_postProcess;
	GpuProfiler m_gpuProfiler;

	// Замер среднего времени GPU за несколько секунд: после прогрева — одной строкой в log.txt («GPU average», её
	// печатает Tools/run.ps1, чтобы сравнивать производительность до и после изменений), и по команде stat gpu
	struct GpuAverage
	{
		std::string name;
		double sum = 0.0;
		uint32_t count = 0;
	};
	struct GpuMeasure
	{
		std::vector<GpuAverage> averages;	// первый элемент — весь кадр
		std::chrono::steady_clock::time_point start;
		std::chrono::duration<float> duration;
		std::function<void( const std::string& )> done;
	};
	// Кадр в замеры; законченные — строкой в done
	void accumulateGpuMeasures();
	// Строка замера после прогрева — в log.txt (из лямбды LOG записал бы её имя вместо функции)
	void logGpuAverage( const std::string& line );
	std::string gpuMeasureLine( const GpuMeasure& measure ) const;

	std::vector<GpuMeasure> m_gpuMeasures;
	uint32_t m_frameIndex = 0;
};

}
