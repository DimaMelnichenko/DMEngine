#pragma once

#include "DMShader.h"

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
class PBRMaterial : public DMShader
{
public:
	PBRMaterial();
	~PBRMaterial();
	void setParams( const PropertyContainer& ) override;
	MaterialRenderState renderState( const PropertyContainer& params ) const override;
	int phaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const override;
	bool supportsInstancing() const override;
	int depthPhaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const override;

private:
	// Константный буфер b2 (PS и VS), раскладка как у PBRMaterialBuffer в Shaders/pbr_material.sh
	struct alignas( 16 ) PSParam
	{
		XMFLOAT4 baseColorFactor;
		XMFLOAT3 emissiveFactor;
		float metallic;
		float roughness;
		float normalScale;
		float normalGreenUp;
		float occlusionStrength;
		float alphaCutoff;
		float windWeight;	// отклик на ветер уровня; вершинному шейдеру (Shaders/wind.sh)
		float padding[2];
		XMFLOAT3 diffuseTransmissionColorFactor;
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

	// Вершинный шейдер фазы: обычный, с матрицами экземпляров (INST_MATRIX) или со сменой LOD дизерингом (LOD_DITHER)
	enum VertexVariant { vertexDefault, vertexInstanced, vertexLodDither, vertexVariantCount };
	static VertexVariant vertexVariant( const ShaderPhaseOptions& options );

	// Номера фаз (DMShader::createPhase): цвет — по [вершинный шейдер][отсечение по альфе][отсечение дизерингом],
	// глубина — по [вершинный шейдер][Masked]: вершинный «только глубина» без пиксельного шейдера или с mainDepth.
	// Дизеринг пиксельного шейдера — только с вершинным шейдером LOD_DITHER
	bool m_instancing = false;
	int m_colorPhases[vertexVariantCount][2][2] = {};
	int m_depthPhases[vertexVariantCount][2] = {};

	bool innerInitialize() override;
	std::vector<VertexElement> initLayouts() override;

	Buffer m_psCB;
};

}
