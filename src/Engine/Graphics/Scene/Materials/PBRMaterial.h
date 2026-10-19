#pragma once

#include "Material.h"
#include "Shaders\pbr_material_constants.h"

namespace GS
{

// Материал PBR metallic/roughness (как Default Lit в UE5 и материал glTF 2.0), шейдер Shaders/PBRLit.ps.
// Параметры названы как в glTF: текстуры BaseColor, MetallicRoughness (G — roughness, B — metallic), Normal,
// Occlusion (R), Emissive и множители к ним. Текстура 0 — «не задана»: вместо неё белая или плоская нормаль.
// Режим — тоже как в glTF: AlphaMode (0 OPAQUE, 1 MASK, 2 BLEND), AlphaCutoff, DoubleSided. Варианты шейдеров —
// перестановки по ключу (биты ShaderPermutation, defines — vertexDefines / pixelDefines): для MASK — пиксельный шейдер
// с отсечением (define ALPHA_MASK; у непрозрачных отсечения нет, и ранняя проверка глубины работает), вершинный для
// инстансинга (INST_MATRIX), «только глубина» для теней и depth prepass — вершинный DEPTH_ONLY (позиция и UV, без
// нормалей), непрозрачные — без пиксельного шейдера, MASK — точка входа mainDepth (только clip). Смена LOD дизерингом
// (DitheredLODTransition, как Dithered LOD Transition в UE) — LOD_DITHER: вершинный шейдер отдаёт долю перехода,
// пиксельные отсекают свою долю пикселей (Shaders/lod_dither.sh)
class PBRMaterial : public Material
{
public:
	PBRMaterial( uint32_t id, const std::string& name );
	bool initialize() override;
	std::vector<VertexElement> initLayouts() override;
	void setParams( const PropertyContainer& ) override;
	MaterialRenderState renderState( const PropertyContainer& params ) const override;
	bool supportsInstancing() const override;
	// Перестановки экземпляров расстановки — вершинный шейдер с INST_POS, INST_SCALE, INST_ROTATE (обычный и LOD_DITHER,
	// у каждого — «только глубина»); у материала, вершинный шейдер которого из базы уже такой (PBRInstance), — он сам
	bool enablePlacedInstances() override;
	// Перестановки запекания: пиксельный шейдер mainBake (и с отсечением по альфе) с обычным вершинным шейдером
	bool enableImpostorBake() override;
	// Перестановки скиннинга — вершинный шейдер с SKINNED (обычный, LOD_DITHER и с матрицами экземпляров — стадо одним
	// вызовом; у каждого — «только глубина»)
	bool enableSkinning() override;

	// Пропускание вперёд по умолчанию (TransmissionForward, TransmissionForwardRoughness; как default_value
	// в MaterialParameterDef): у всех пропускающих материалов уровня — листья, травинки, лепестки
	static constexpr float defaultTransmissionForward = 0.75f;
	static constexpr float defaultTransmissionForwardRoughness = 0.775f;
	// Качание всего дерева (PBRTree, слой Global ветра — Shaders/wind.sh): наклон верхушки при силе ветра 1, градусы, рост
	// наклона с долей высоты, частота, Гц; их же берёт импостер дерева (ImpostorMaterial)
	static constexpr float defaultWindGlobalAngle = 1.5f;
	static constexpr float defaultWindGlobalExponent = 1.5f;
	static constexpr float defaultWindGlobalFrequency = 0.25f;

private:
	// Перестановки вершинного шейдера с признаками features (instanced, placed, lodDither): цвет — с отсечением по
	// альфе и без, при смене LOD — ещё с отсечением дизерингом и без (после depth prepass); «только глубина» — с
	// отсечением по альфе и без (Masked), при смене LOD — всегда с дизерингом
	bool addVertexVariant( uint32_t features );
	// Шейдеры перестановки key (компилируются, если таких ещё нет) и её фаза
	bool createPermutation( uint32_t key );
	// Defines перестановки: исходные из базы плюс признаки ключа (у материала без инстансинга или с экземплярами
	// расстановки в базе лишние признаки ничего не добавляют — ключ ведёт к той же фазе)
	std::string vertexDefines( uint32_t key ) const;
	std::string pixelDefines( uint32_t key ) const;

	ShaderSource m_vertexSource;	// шейдеры из базы — основа перестановок
	ShaderSource m_pixelSource;
	bool m_instancing = false;	// вершинный шейдер из базы без экземпляров (INST_*): есть вариант INST_MATRIX
	bool m_placed = false;		// перестановки экземпляров расстановки есть
	bool m_skinned = false;		// перестановки скиннинга есть

	Buffer m_psCB;	// PBRMaterialBuffer (b2): пиксельному и вершинному шейдеру
};

}
