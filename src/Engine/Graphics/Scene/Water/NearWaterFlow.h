#pragma once

#include <string>
#include "ComputeShader.h"
#include "D3D\DMStructuredBuffer.h"
#include "Level\LevelSettings.h"
#include "Shaders\water_near_flow_constants.h"

namespace GS
{

class StaticWater;
class WaterField;
struct FrameContext;
struct FrameParameters;
struct TerrainHeight;

// Подробное течение у камеры (docs/water.md, «Подробное течение у камеры»): окно мелкой сетки (по умолчанию 10 × 10 м,
// ячейка 6,25 см) вокруг камеры, каждый кадр — та же модель, что течение статичной воды (Shaders/water_flow.cs с FLOW_NEAR:
// мелкая вода под «жёсткой крышкой» на сетке MAC), вложенная в течение детальной сетки 0,25 м, как то — в сетку 1 м.
// Камни русла — полуэллипсоиды на своей сетке, камень выше воды — препятствие (вода обходит его), вихри за камнями и на
// поворотах держит удержание завихрённости. Окно привязано к миру (NearWindow): сдвигается вслед за камерой целыми
// ячейками, вошедшие ячейки начинаются с течения детальной сетки, остальные считаются дальше; шаги — по времени игры
// (с -freezetime их нет). Итог — ресурс сцены SLOT_WATER_NEAR и константа кадра cb_waterNear
class NearWaterFlow
{
public:
	using Settings = WaterSimulationSettings::NearFlow;

	bool initialize();
	// Окно — к камере кадра, шаги за время игры с прошлого кадра: детальные плитки и их течение, камни — у water, константы
	// поля (b4) — у field по settings; без детальной воды (или выключено) — окна нет
	void compute( const FrameContext& frame, const WaterSimulationSettings& settings, WaterField& field, const StaticWater& water,
				  const TerrainHeight& height );
	// Окно — шейдерам (cb_waterNear); нет окна — нули
	void frameConstants( FrameParameters& constants ) const;
	bool active() const { return m_active; }
	// Окно — растром (строка 0 — дальний край по z, как у остальных выгрузок): <prefix>_near_depth.dds, _near_vx, _near_vz.
	// Ждёт GPU
	bool exportState( const std::string& prefix, std::string& reason );

private:
	static constexpr uint32_t maxStones = WATER_NEAR_MAX_STONES;
	// Текстуры окна size² (заново при смене стороны окна)
	bool createTextures( uint32_t size );

	ComputeShader m_init;
	ComputeShader m_faces;
	ComputeShader m_advect;
	ComputeShader m_vorticity;
	ComputeShader m_viscosity;
	ComputeShader m_nudge;
	ComputeShader m_divergence;
	ComputeShader m_red;
	ComputeShader m_black;
	ComputeShader m_project;
	ComputeShader m_output;
	Buffer m_flowBuffer;		// WaterFlowBuffer (b5)
	Buffer m_detailBuffer;		// WaterDetailFlowBuffer (b6)
	Buffer m_nearBuffer;		// WaterNearFlowBuffer (b7)
	DMStructuredBuffer m_stones;	// камни русла в окне: центр основания, поперечник

	// Текстуры окна: вид ячейки и u₀, R и глубина без камней, скорость граней, перенос, давление, расхождение, завихрённость,
	// итог для шейдеров
	Texture m_info, m_info2, m_velocity, m_advected, m_pressure, m_divergenceTexture, m_vorticityTexture, m_result;
	StorageView m_infoUAV, m_info2UAV, m_velocityUAV, m_advectedUAV, m_pressureUAV, m_divergenceUAV, m_vorticityUAV, m_resultUAV;
	ShaderView m_resultView;
	uint32_t m_size = 0;		// ячеек по стороне окна
	float m_step = 0.0f;		// ячейка, м
	DirectX::XMINT2 m_origin = DirectX::XMINT2( 0, 0 );
	float m_gameTime = 0.0f;	// время игры прошлого кадра
	bool m_reset = true;		// следующий кадр — окно заново
	bool m_active = false;
	bool m_initialized = false;
};

}
