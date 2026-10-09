#pragma once

#include <memory>
#include <vector>
#include "SceneObject.h"
#include "DMComputeShader.h"
#include "ShaderProgram.h"
#include "Level\LevelSettings.h"
#include "Terrain\GridMesh.h"
#include "Terrain\TerrainHeightSource.h"

namespace GS
{

// Частицы уровня на GPU — как Niagara в UE и VFX Graph в Unity: эмиттеры уровня (строки LevelParticleEmitters →
// ParticleEmitters, Tools/particle_emitter.py) с пулом частиц у каждого. Рождение, движение (тяжесть, сопротивление,
// ветер уровня, вихри, течение воды), столкновение с рельефом и гибель — в compute (Shaders/particles.cs), без
// перестройки списков на CPU: стек мёртвых индексов и список живых кадра; рисует эмиттер один ExecuteIndirect со
// счётчиком живых (particles.vs / particles.ps) в проходе transparent с глубиной сцены (мягкий край). Окно GUI
// «Particles» — подокно на эмиттер. Подробно — docs/particles.md
class ParticleSystem : public SceneObject
{
public:
	using Settings = ParticleEmitterSettings;

	ParticleSystem();
	// terrain — высота и маски для полей вокруг камеры и столкновений; nullptr — у уровня нет террейна (такие эмиттеры
	// пропускаются)
	bool initialize( const std::vector<Settings>& emitters, const TerrainHeightSource* terrain );
	bool initialized() const { return m_initialized; }

	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	void warmPipelines( const PassStates& states ) override;
	PropertyContainer* properties() override;
	bool savedWithLevel() const override { return true; }
	// Эмиттеры с настройками из GUI — для сохранения уровня (строки ParticleEmitters)
	std::vector<Settings> emitterSettings() const;

private:
	// Раскладка — cbuffer ParticleEmitterBuffer (b4) в Shaders/particles.sh
	struct alignas( 16 ) EmitterParameters
	{
		DirectX::XMFLOAT3 origin;
		uint32_t spawn;
		float radius;
		float heightMin;
		float heightMax;
		uint32_t capacity;
		uint32_t spawnCount;
		uint32_t frame;
		uint32_t emitterIndex;
		float timeStep;
		float lifetimeMin;
		float lifetimeMax;
		float sizeStart;
		float sizeEnd;
		DirectX::XMFLOAT3 color;
		float alpha;
		DirectX::XMFLOAT3 velocity;
		float velocitySpread;
		float gravity;
		float drag;
		float wind;
		float curl;
		float curlScale;
		float waterFlow;
		float waterSpeed;
		uint32_t collide;
		float fadeIn;
		float fadeOut;
		float transmission;
		float emissive;
		uint32_t shape;
		uint32_t hasMask;
		uint32_t hasTerrain;
		float viewportHeight;
	};

	// cbuffer TerrainHeightBuffer (b5) в Shaders/terrain_height.sh
	struct alignas( 16 ) TerrainParameters
	{
		float worldSize;
		float heightMultiplier;
		float heightOffset;
		float detailTileSize;	// детальная земля у русел (terrain_detail.sh); 0 — нет
	};

	struct Emitter
	{
		Settings settings;
		PropertyContainer properties;
		uint32_t capacity = 0;
		float pending = 0.0f;			// дробная доля рождений, не отданная прошлым кадрам
		const ShaderView* mask = nullptr;
		Buffer particles;				// Particle × capacity (48 байт, Shaders/particles.sh)
		Buffer dead;					// uint × capacity: стек мёртвых индексов
		Buffer alive;					// uint × capacity: живые кадра — экземпляры вызова
		Buffer state;					// команда ExecuteIndirect, число команд, вершина стека мёртвых
		StorageView particlesUAV;
		StorageView deadUAV;
		StorageView aliveUAV;
		StorageView stateUAV;
		ShaderView particlesView;
		ShaderView aliveView;
		Buffer constants;
	};

	// Байт буфера состояния (Shaders/particles.sh): 24 — команда, 4 — число команд, 4 — вершина стека мёртвых
	static constexpr uint32_t stateBytes = 32;
	static constexpr uint32_t particleBytes = 48;

	bool createEmitter( Emitter& emitter, uint32_t index );
	// Текущие значения GUI эмиттера
	Settings current( const Emitter& emitter ) const;
	void setParameters( Emitter& emitter, uint32_t index, uint32_t spawnCount, float timeStep );
	bool enabled( const Emitter& emitter ) const;

	PropertyContainer m_properties;
	std::vector<std::unique_ptr<Emitter>> m_emitters;
	const TerrainHeightSource* m_terrain = nullptr;
	Buffer m_terrainBuffer;
	DMComputeShader m_initShader;
	DMComputeShader m_resetShader;
	DMComputeShader m_emitShader;
	DMComputeShader m_updateShader;
	ShaderProgram m_program;
	int m_phase = -1;
	GridMesh m_quad;
	uint32_t m_frame = 0;			// шагов симуляции — зерно случайных чисел
	float m_timeStep = 0.0f;		// шаг последнего кадра, с
	bool m_initialized = false;
};

}
