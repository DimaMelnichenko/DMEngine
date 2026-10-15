#pragma once

#include <vector>
#include "SceneObject.h"
#include "DMComputeShader.h"
#include "Terrain\TerrainHeightSource.h"
#include "Shaders\foliage_interaction_constants.h"

namespace GS
{

// Трава расступается — как в Ghost of Tsushima и Horizon (буфер смещений вокруг камеры, трава гнётся от него в вершинном
// шейдере). Поле следов вокруг камеры: в клетке — куда и насколько наклонить растение (вектор XZ, длина 0…1). Каждый кадр
// compute (Shaders/foliage_interaction.cs) гасит прежний след — растения распрямляются за «Recovery time» — и рисует
// новые: капсулы от тех, кто идёт по траве (сейчас — камера, когда она у земли: ходьба или низкий полёт; потом —
// звери, addInteractor). Растения с WindWeight > 0, кроме деревьев, наклоняются от корня по полю (Shaders/foliage_interaction.sh,
// vertexWorldPosition в LightShader.vs) — во всех проходах одинаково: глубина, тени, цвет. Поле — ресурс сцены
// SLOT_FOLIAGE_INTERACTION, окно — константы кадра cb_foliageInteraction. Окно GUI «Foliage interaction» (не сохраняется
// с уровнем, как «Walk»). Подробно — docs/wind.md, «Трава расступается»
class FoliageInteraction : public SceneObject
{
public:
	// Капсула следа: отрезок по земле (XZ, м мира) с радиусом — полный наклон ближе него к оси, дальше на edge сходит на
	// нет; strength — доля полного наклона 0…1; направление движения (нормированное, 0 — стоит) — трава под ногами
	// ложится вперёд
	struct Interactor
	{
		DirectX::XMFLOAT2 from;
		DirectX::XMFLOAT2 to;
		float radius = 0.45f;
		float edge = 0.22f;
		float strength = 1.0f;
		DirectX::XMFLOAT2 direction = DirectX::XMFLOAT2( 0.0f, 0.0f );
	};
	static constexpr uint32_t maxInteractors = FOLIAGE_MAX_INTERACTORS;

	FoliageInteraction();
	// terrain — высота земли под камерой (CPU): камера давит траву, только когда она у земли
	bool initialize( const TerrainHeightSource* terrain );
	bool initialized() const { return m_initialized; }

	// Окно поля вокруг камеры и следы кадра — до констант кадра (их пишет Renderer::preparePipeline после Scene::update)
	void update( const FrameContext& frame ) override;
	// Константы кадра cb_foliageInteraction: начало окна X, Z, м; 1 / его сторона, 1/м (0 — поля нет); наибольший наклон, рад
	void frameConstants( const RenderView& view, FrameParameters& constants ) override;
	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override {}
	PropertyContainer* properties() override { return &m_properties; }
	// Ещё один след в этом кадре (звери, позже), до compute
	void addInteractor( const Interactor& interactor );

private:

	bool enabled() const;
	// Камера у земли — след от прошлого места ног к нынешнему
	void addCameraInteractor( const FrameContext& frame );

	bool m_initialized = false;
	const TerrainHeightSource* m_terrain = nullptr;
	DMComputeShader m_shader;
	Buffer m_constants;
	// Поле дважды: прошлый кадр читается, нынешний пишется (без чтения UAV форматов R16G16)
	Texture m_fields[2];
	ShaderView m_fieldSRV[2];
	StorageView m_fieldUAV[2];
	uint32_t m_current = 0;
	bool m_cleared = false;			// первый кадр — прошлого поля нет

	int32_t m_origin[2] = { 0, 0 };
	int32_t m_previousOrigin[2] = { 0, 0 };
	std::vector<Interactor> m_interactors;
	bool m_hasFeet = false;			// прошлое место ног камеры — для отрезка следа
	DirectX::XMFLOAT2 m_previousFeet = DirectX::XMFLOAT2( 0.0f, 0.0f );
	float m_frameSeconds = 0.0f;

	PropertyContainer m_properties;
};

}
