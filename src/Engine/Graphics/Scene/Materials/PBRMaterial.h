#pragma once

#include "Material.h"

namespace GS
{

// Материал PBR metallic/roughness (как Default Lit в UE5 и материал glTF 2.0), шейдер Shaders/PBRLit.ps.
// Параметры названы как в glTF: текстуры BaseColor, MetallicRoughness (G — roughness, B — metallic), Normal,
// Occlusion (R), Emissive и множители к ним. Текстура 0 — «не задана»: вместо неё белая или плоская нормаль.
// Режим — тоже как в glTF: AlphaMode (0 OPAQUE, 1 MASK, 2 BLEND), AlphaCutoff, DoubleSided. Для MASK материал
// сам собирает второй вариант пиксельного шейдера с отсечением (define ALPHA_MASK): у непрозрачных отсечения нет,
// и ранняя проверка глубины работает. Так же — вариант вершинного шейдера для инстансинга (define INST_MATRIX)
// и фазы «только глубина» для теней: вершинный шейдер с define DEPTH_ONLY (позиция и UV, без нормалей), непрозрачные —
// без пиксельного шейдера, MASK — точка входа mainDepth (только clip). Смена LOD дизерингом (DitheredLODTransition, как
// Dithered LOD Transition в UE) — ещё варианты с define LOD_DITHER: вершинный шейдер отдаёт долю перехода, пиксельные
// отсекают свою долю пикселей (Shaders/lod_dither.sh)
class PBRMaterial : public Material
{
public:
	PBRMaterial( uint32_t id, const std::string& name );
	bool initialize() override;
	std::vector<VertexElement> initLayouts() override;
	void setParams( const PropertyContainer& ) override;
	MaterialRenderState renderState( const PropertyContainer& params ) const override;
	int phaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const override;
	bool supportsInstancing() const override;
	// Вершинные шейдеры с INST_POS, INST_SCALE, INST_ROTATE (обычный и LOD_DITHER, у каждого — «только глубина») и их
	// фазы; у материала, вершинный шейдер которого из базы уже такой (PBRInstance), — он сам
	bool enablePlacedInstances() override;
	// Пиксельный шейдер mainBake (и с отсечением по альфе) с обычным вершинным шейдером
	bool enableImpostorBake() override;
	std::vector<int> bakePhases() const override;
	int depthPhaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const override;
	// Фазы «только глубина» всех вариантов, в том числе с пиксельным шейдером mainDepth (Masked)
	std::vector<int> depthPhases() const override;

private:
	// Константный буфер b2 (PS и VS), раскладка как у PBRMaterialBuffer в Shaders/pbr_material.sh
	struct alignas( 16 ) PSParam
	{
		DirectX::XMFLOAT4 baseColorFactor;
		DirectX::XMFLOAT3 emissiveFactor;
		float metallic;
		float roughness;
		float normalScale;
		float normalGreenUp;
		float occlusionStrength;
		float alphaCutoff;
		float windWeight;	// отклик на ветер уровня; вершинному шейдеру (Shaders/wind.sh)
		float padding[2];
		DirectX::XMFLOAT3 diffuseTransmissionColorFactor;
		float diffuseTransmissionFactor;
		// Ветер дерева (материал PBRTree, define WIND_TREE, Shaders/wind.sh): углы — радианы при силе ветра 1, частоты — Гц
		float windGlobalAngle;
		float windGlobalExponent;
		float windGlobalFrequency;
		float windBranchAngle;
		float windBranchFrequency;
		float windTwigAngle;
		float windTwigFrequency;
		float windRippleAmplitude;	// м
		float windRippleFrequency;
		float windPadding[3];
	};
	static_assert( sizeof( PSParam ) == 128, "PBRMaterialBuffer layout" );

	// Вершинный шейдер фазы: обычный, с матрицами экземпляров (INST_MATRIX), со сменой LOD дизерингом (LOD_DITHER) и
	// экземпляры расстановки (INST_POS, INST_SCALE, INST_ROTATE) — обычный и с дизерингом
	enum VertexVariant { vertexDefault, vertexInstanced, vertexLodDither, vertexPlaced, vertexPlacedLodDither, vertexVariantCount };
	static VertexVariant vertexVariant( const ShaderPhaseOptions& options );
	// Фазы варианта: цвет — пиксельный шейдер 0 или 1 (отсечение по альфе), с дизерингом — 3 или 4; глубина — без
	// пиксельного шейдера или с mainDepth (2), с дизерингом — всегда с mainDepth (5 или 6)
	bool createVariantPhases( VertexVariant variant, int colorShader, int depthShader );

	// Номера фаз (ShaderProgram::createPhase): цвет — по [вершинный шейдер][отсечение по альфе][отсечение дизерингом],
	// глубина — по [вершинный шейдер][Masked]: вершинный «только глубина» без пиксельного шейдера или с mainDepth.
	// Дизеринг пиксельного шейдера — только с вершинным шейдером LOD_DITHER. −1 — варианта нет (placed не включён)
	bool m_instancing = false;
	bool m_placed = false;		// варианты экземпляров расстановки есть
	int m_vertexShaderCount = 0;	// вершинных шейдеров в программе — номер следующего
	int m_colorPhases[vertexVariantCount][2][2] = {};
	int m_depthPhases[vertexVariantCount][2] = {};
	int m_bakePhases[2] = { -1, -1 };	// запекание импостера: [Masked]

	Buffer m_psCB;
};

}
