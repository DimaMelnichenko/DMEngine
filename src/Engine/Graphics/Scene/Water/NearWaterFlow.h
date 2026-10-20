#pragma once

#include <string>
#include "ComputeShader.h"
#include "D3D\DMStructuredBuffer.h"
#include "Level\LevelSettings.h"
#include "StoneTop.h"
#include "Shaders\water_near_flow_constants.h"

namespace GS
{

class StaticWater;
class WaterFoam;
struct WaterField;
struct FrameContext;
struct FrameParameters;
struct TerrainHeight;

// Подробное течение у камеры (docs/water.md, «Подробное течение у камеры»): окно мелкой сетки (по умолчанию 10 × 10 м,
// ячейка 6,25 см) вокруг камеры, каждый кадр — та же модель, что течение статичной воды (Shaders/water_flow.cs с FLOW_NEAR:
// мелкая вода под «жёсткой крышкой» на сетке MAC), вложенная в течение детальной сетки 0,25 м, как то — в сетку 1 м.
// Камни русла — по верху их моделей (StoneTop от расстановки), камень выше воды — препятствие (вода обходит его), вихри за
// камнями и на поворотах держит удержание завихрённости. Окно привязано к миру (NearWindow): сдвигается вслед за камерой целыми
// ячейками, вошедшие ячейки начинаются с течения детальной сетки, остальные считаются дальше; шаги — по времени игры
// (с -freezetime их нет). Итог — ресурс сцены SLOT_WATER_NEAR и константа кадра cb_waterNear. Своя пена окна
// (Shaders/water_near_foam.cs: рождается у камней и на сдвиге, её несёт течение окна; за краем окна — поле пены 0,25 м) —
// ресурс сцены SLOT_WATER_NEAR_FOAM, доля в материале — cb_waterNearFoam
class NearWaterFlow
{
public:
	using Settings = WaterSimulationSettings::NearFlow;

	bool initialize();
	// Окно — к камере кадра, шаги за время игры с прошлого кадра: детальные плитки и их течение, камни — у water, константы
	// поля (b4) — у field по settings, пена за краем окна — поле пены foam; без детальной воды (или выключено) — окна нет
	void compute( const FrameContext& frame, const WaterSimulationSettings& settings, WaterField& field, const StaticWater& water,
				  const TerrainHeight& height, const WaterFoam& foam );
	// Окно — шейдерам (cb_waterNear); нет окна — нули
	void frameConstants( FrameParameters& constants ) const;
	// Камни русел расстановки с верхом по модели: препятствия окна (на GPU — в следующем compute); до них камней в окне нет
	void setStoneTops( std::vector<StoneTop>&& tops );
	bool active() const { return m_active; }
	// Ячеек по стороне окна, у которого есть текстуры (0 — окна не было): сетка поверхности окна (WaterSurface) — по ним,
	// рисует ли её и где — по константам кадра (cb_waterNear)
	uint32_t windowCells() const { return m_size; }
	// Окно — растром (строка 0 — дальний край по z, как у остальных выгрузок): <prefix>_near_surface.dds (подъём поверхности
	// по давлению, м), _near_vx, _near_vz, _near_passage, _near_foam. Ждёт GPU
	bool exportState( const std::string& prefix, std::string& reason );

private:
	static constexpr uint32_t maxStones = WATER_NEAR_MAX_STONES;
	// Текстуры окна size² (заново при смене стороны окна)
	bool createTextures( uint32_t size );
	// Пена окна за время elapsed (0 — та же, вошедшие ячейки — из поля foam): после расчёта течения, в том же кадре
	void stepFoam( const WaterSimulationSettings& settings, const WaterFoam& foam, float elapsed, const WaterNearFlowBuffer& window );

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
	ComputeShader m_foamStep;
	Buffer m_flowBuffer;		// WaterFlowBuffer (b5)
	Buffer m_detailBuffer;		// WaterDetailFlowBuffer (b6)
	Buffer m_nearBuffer;		// WaterNearFlowBuffer (b7)
	Buffer m_foamBuffer;		// WaterFoamBuffer (b5) прохода пены
	Buffer m_foamWindowBuffer;	// WaterNearFlowBuffer (b7) прохода пены: окно заново — и после кадров без пены
	DMStructuredBuffer m_stones;	// камни русла в окне: по два float4 — квадрат верха и его номер
	// Камни русел: квадрат верха (угол x, z, сторона, низ) — для отбора в окно; высоты всех квадратов — на GPU по две
	// половинной точности в uint (m_stoneTopData ждёт загрузки, пока m_stoneTopsPending)
	std::vector<DirectX::XMFLOAT4> m_stoneBounds;
	std::vector<uint32_t> m_stoneTopData;
	bool m_stoneTopsPending = true;
	Buffer m_stoneTops;
	ShaderView m_stoneTopsView;

	// Текстуры окна: вид ячейки и u₀, R и глубина без камней, скорость граней, перенос, давление, расхождение, завихрённость,
	// итог для шейдеров
	Texture m_info, m_info2, m_velocity, m_advected, m_pressure, m_divergenceTexture, m_vorticityTexture, m_result;
	StorageView m_infoUAV, m_info2UAV, m_velocityUAV, m_advectedUAV, m_pressureUAV, m_divergenceUAV, m_vorticityUAV, m_resultUAV;
	ShaderView m_resultView;
	// Пена окна (R16F): два поля по очереди — шаг читает прошлое, пишет другое
	Texture m_foam[2];
	StorageView m_foamUAV[2];
	ShaderView m_foamView[2];
	uint32_t m_foamCurrent = 0;
	bool m_foamReset = true;	// прошлого поля пены нет (окно заново, поля пены 0,25 м ещё нет)
	float m_foamTime = 0.0f;	// время игры, которое пена окна шла с последнего «заново», с: 0 — она ещё — поле 0,25 м
	float m_foamShare = 0.0f;	// доля пены окна в материале (cb_waterNearFoam)
	uint32_t m_size = 0;		// ячеек по стороне окна
	float m_step = 0.0f;		// ячейка, м
	DirectX::XMINT2 m_origin = DirectX::XMINT2( 0, 0 );
	float m_gameTime = 0.0f;	// время игры прошлого кадра
	bool m_reset = true;		// следующий кадр — окно заново
	bool m_active = false;
	bool m_initialized = false;
};

}
