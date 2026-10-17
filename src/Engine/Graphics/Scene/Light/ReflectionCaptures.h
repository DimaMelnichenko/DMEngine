#pragma once

#include <memory>
#include <vector>
#include "SceneObject.h"
#include "CubePrefilter.h"
#include "ComputeShader.h"
#include "D3D\CubeTarget.h"
#include "Level\LevelSettings.h"

namespace GS
{

// Карты отражений места уровня — как Sphere Reflection Capture в UE: строки LevelReflectionCaptures (точка съёмки, радиус
// влияния, размер грани; Tools/reflection_capture.py), у каждой — кубическая карта сцены из точки: съёмка сцены по грани за
// кадр (SceneObject::requestCapture) без неба, покрытие сценой — в альфу по глубине грани (Shaders/reflection_capture_coverage.cs),
// затем префильтр по шероховатости (CubePrefilter, мипы — как у освещения неба). В кадре — ближайшая к камере готовая карта:
// ресурс сцены t131 и константы cb_reflectionCapture* (Shaders/reflection_capture.sh); где экранные отражения воды
// промахнулись — сцена из неё с поправкой параллакса по сфере влияния и небо сквозь непокрытое, за её радиусом — небо. Снимается заново после загрузки, правки точки или размера грани,
// кнопкой «Recapture» и когда сдвинулось светило теней (время суток). Окно «Reflection captures» — подокно на карту.
// Подробно — docs/reflection_captures.md
class ReflectionCaptures : public SceneObject
{
public:
	using Settings = ReflectionCaptureSettings;
	// Мипы префильтра — как у освещения неба: шероховатость мипа m — m / (mipCount − 1) (specularMipCount в Shaders/ibl.sh)
	static constexpr uint32_t mipCount = 6;
	// Сдвиг светила теней, после которого карты снимаются заново (небо, свет и тени в них — того времени суток), градусы
	static constexpr float recaptureLightAngle = 1.0f;

	ReflectionCaptures();
	bool initialize( const std::vector<Settings>& captures );
	bool initialized() const { return !m_captures.empty(); }

	// Правки окна и сдвиг светила — съёмка заново
	void update( const FrameContext& frame ) override;
	// Ближайшая к камере готовая карта — в константы кадра
	void frameConstants( const RenderView& view, FrameParameters& constants ) override;
	// Снятые грани — префильтр; карта кадра — в слот сцены t131
	void compute( const FrameContext& frame ) override;
	// Следующая грань карты, которую снимают
	bool requestCapture( const FrameContext& frame, SceneCapture& capture ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override {}
	PropertyContainer* properties() override { return &m_properties; }
	bool savedWithLevel() const override { return true; }
	// Карты с настройками из окна — для «Save level» (строки LevelReflectionCaptures)
	std::vector<Settings> captureSettings() const;

private:
	static constexpr int32_t faceCount = 6;

	struct Capture
	{
		Settings settings;					// с которыми снята: точка и размер грани
		PropertyContainer properties;
		uint64_t appliedRevision = 0;		// номер правки окна, по которому сверены точка и размер грани
		CubeTarget source;					// грани сцены (мип 0 — съёмка) и мипы для выборки префильтра
		CubeTarget specular;				// префильтр — его читают шейдеры
		Texture depth;						// глубина съёмки грани — по ней покрытие
		TargetView depthTarget;
		ShaderView depthView;
		int32_t nextFace = 0;				// следующая грань съёмки; faceCount — снимать нечего
		bool prefilterPending = false;		// все грани сняты — префильтр в compute этого кадра
		bool ready = false;					// префильтр готов хоть раз
		float exposure = 0.0f;				// экспозиция идущей съёмки (её первой грани)
		float shownExposure = 0.0f;			// экспозиция, с которой снята показанная карта
		DirectX::XMFLOAT3 toLight = DirectX::XMFLOAT3( 0.0f, 1.0f, 0.0f );	// светило теней при начале съёмки
	};

	bool createResources( Capture& capture );
	// Текущие значения окна карты
	Settings current( const Capture& capture ) const;
	// Съёмка заново с первой грани (прежняя карта показывается до конца префильтра новой)
	static void restart( Capture& capture );
	// Грань снята: альфа — 1 там, где сцена есть (глубина не на дальней плоскости), остальное — 0
	void writeCoverage( const Capture& capture, int32_t face );

	PropertyContainer m_properties;
	std::vector<std::unique_ptr<Capture>> m_captures;
	CubePrefilter m_prefilter;
	ComputeShader m_coverageShader;		// покрытие грани по глубине съёмки
	Buffer m_coverageConstants;
	const Capture* m_shown = nullptr;		// карта этого кадра (frameConstants) — её compute привязывает в t131
};

}
