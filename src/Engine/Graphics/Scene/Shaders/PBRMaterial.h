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
// и фазы «только глубина» для теней: непрозрачные — без пиксельного шейдера, MASK — точка входа mainDepth (только clip)
class PBRMaterial : public DMShader
{
public:
	PBRMaterial();
	~PBRMaterial();
	void setParams( const PropertyContainer& ) override;
	MaterialRenderState renderState( const PropertyContainer& params ) const override;
	int phaseFor( const PropertyContainer& params, bool instanced = false ) const override;
	bool supportsInstancing() const override;
	int depthPhaseFor( const PropertyContainer& params, bool instanced = false ) const override;

private:
	// Константный буфер PS b2, раскладка как у PBRMaterialBuffer в Shaders/PBRLit.ps
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
	};

	// Номера фаз (DMShader::createPhase) по [инстансный вершинный шейдер][Masked]: цвет — пиксельный шейдер без
	// отсечения или с ним, глубина — без пиксельного шейдера или mainDepth
	bool m_instancing = false;
	int m_colorPhases[2][2] = {};
	int m_depthPhases[2][2] = {};

	bool innerInitialize() override;
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts() override;

	com_unique_ptr<ID3D11Buffer> m_psCB;
};

}
