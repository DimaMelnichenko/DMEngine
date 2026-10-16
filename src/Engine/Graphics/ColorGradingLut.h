#pragma once

#include <cstdint>
#include "ComputeShader.h"
#include "D3D\GpuProfiler.h"
#include "Scene\Level\LevelSettings.h"
#include "Shaders\color_grading_lut.h"

namespace GS
{

// Таблица цветокоррекции и тонмаппинга — как Combine LUTs в UE: объёмная текстура COLOR_GRADING_LUT_SIZE³, вход — цвет после
// экспозиции в log₂ по каналам (Shaders/color_grading.sh), в ячейке — цвет экрана после баланса белого, цветокоррекции по
// теням, полутонам и светам (Color Grading в Post Process Volume UE) и кривой тонмаппинга (Shaders/color_grading_lut.cs).
// Строится заново только после правки настроек; тонмаппинг кадра (PostProcess, Shaders/tonemap.ps) берёт из неё одну
// выборку на пиксель
class ColorGradingLut
{
public:
	static constexpr uint32_t size = COLOR_GRADING_LUT_SIZE;

	bool initialize();
	// Таблица заново, если сменился номер правки настроек (revision — PropertyContainer::revision окна, у которого они):
	// проход compute перед тонмаппингом, время — область profiler
	void update( const ColorGradingSettings& settings, Tonemapper tonemapper, uint64_t revision, GpuProfiler& profiler );
	const ShaderView& view() const { return m_srv; }

private:
	ComputeShader m_shader;
	Texture m_texture;
	StorageView m_uav;
	ShaderView m_srv;
	Buffer m_constants;
	// Номер правки, по которому таблица построена: 0 — ещё не строилась (у свойств номера с 1)
	uint64_t m_revision = 0;
};

}
