#pragma once

#include <string>
#include "Shaders\FullscreenShader.h"
#include "D3D\RenderTarget.h"
#include "D3D\GpuProfiler.h"
#include "Properties\PropertyContainer.h"

namespace GS
{

// Постобработка кадра — цепочка полноэкранных проходов (Shaders/fullscreen.vs) с промежуточными целями (RenderTarget):
// - bloom (J. Jimenez, SIGGRAPH 2014): цвет сцены уменьшается по уровням 1/2 … 1/64 (Shaders/bloom_downsample.ps —
//   с порогом и защитой от «светлячков» на первом), затем уровни увеличиваются и складываются от мелкого к крупному
//   (Shaders/bloom_upsample.ps, BlendState::additive);
// - тонмаппинг (Shaders/tonemap.ps): цвет сцены + bloom, экспозиция, AgX / ACES — в задний буфер.
// Настройки — как Exposure, Tonemapper, Bloom в Post Process Volume UE: строка таблицы PostProcessSettings, на которую
// ссылается уровень (Levels.post_process), меняются в GUI («Post process»). Новый проход (гистограмма яркости, LUT,
// сглаживание) — ещё одна цель и шаг в render()
class PostProcess
{
public:
	enum class Tonemapper : int32_t
	{
		none = 0,	// только ограничение 0…1 — для отладки
		aces = 1,	// ACES (подгонка RRT + ODT, S. Hill) — основа Filmic tonemapper UE
		agx = 2		// AgX — стандартное отображение Blender 4+, мягче уводит яркие цвета в белый
	};

	static constexpr uint32_t bloomLevelCount = 6;
	// Вес каждого следующего, вдвое более широкого уровня относительно предыдущего: вклад уровней убывает от узкого
	// к широкому примерно вшестеро, как веса Bloom1…Bloom6 Tint в UE
	static constexpr float bloomLevelFalloff = 0.6f;

	// Строка PostProcessSettings; без неё — значения по умолчанию
	struct Settings
	{
		float exposureCompensation = 0.0f;	// EV: +1 — вдвое ярче
		Tonemapper tonemapper = Tonemapper::agx;
		float bloomIntensity = 0.05f;		// Bloom Intensity — доля энергии над порогом в свечении; 0 — без bloom
		float bloomThreshold = 1.0f;		// Bloom Threshold, яркость после экспозиции; < 0 — без порога
	};
	// Имя тонмаппинга в базе: None, ACES, AgX (другое — AgX)
	static Tonemapper tonemapperFromName( const std::string& name );
	static const char* tonemapperName( Tonemapper tonemapper );

	bool initialize( const Settings& settings );
	// Текущие значения из GUI — для сохранения уровня
	Settings settings();
	// Рисует цепочку в задний буфер и оставляет его привязанным для GUI; время проходов — области profiler
	void render( GpuProfiler& profiler );
	PropertyContainer* properties();

private:
	// Константный буфер PS b2, раскладка как у PostProcessBuffer в Shaders/tonemap.ps
	struct alignas( 16 ) Parameters
	{
		float exposure;		// множитель 2^EV
		int32_t tonemapper;
		float bloomScale;	// Bloom Intensity / сумма весов уровней
		float padding;
	};

	// Константный буфер PS b2 проходов bloom, раскладка как у BloomBuffer в Shaders/bloom_*.ps
	struct alignas( 16 ) BloomParameters
	{
		XMFLOAT2 sourceTexelSize;
		float exposure;
		float threshold;
		int32_t firstPass;
		float radius;
		float weight;
		float padding;
	};

	void renderBloom( const com_unique_ptr<ID3D11ShaderResourceView>& sceneColor, float exposure, float threshold );
	void drawPass( FullscreenShader& shader, const RenderTarget& target, const com_unique_ptr<ID3D11ShaderResourceView>& source,
				   BloomParameters params, BlendState blend );

	FullscreenShader m_shader;
	FullscreenShader m_bloomDownsample;
	FullscreenShader m_bloomUpsample;
	RenderTarget m_bloom[bloomLevelCount];
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	com_unique_ptr<ID3D11Buffer> m_bloomConstants;
	PropertyContainer m_properties;
};

}
