#pragma once

#include "DMShader.h"

namespace GS
{

// Материал PBR metallic/roughness (как Default Lit в UE5 и материал glTF 2.0), шейдер Shaders/PBRLit.ps.
// Параметры названы как в glTF: текстуры BaseColor, MetallicRoughness (G — roughness, B — metallic), Normal,
// Occlusion (R), Emissive и множители к ним. Текстура 0 — «не задана»: вместо неё белая или плоская нормаль.
// Режим — тоже как в glTF: AlphaMode (0 OPAQUE, 1 MASK, 2 BLEND), AlphaCutoff, DoubleSided. Для MASK материал
// сам собирает второй вариант пиксельного шейдера с отсечением (define ALPHA_MASK): у непрозрачных отсечения нет,
// и ранняя проверка глубины работает
class PBRMaterial : public DMShader
{
public:
	PBRMaterial();
	~PBRMaterial();
	void setParams( const PropertyContainer& ) override;
	MaterialRenderState renderState( const PropertyContainer& params ) const override;
	int phaseFor( const PropertyContainer& params ) const override;

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

	enum PhaseIndex { opaquePhase = 0, maskedPhase = 1 };

	bool innerInitialize() override;
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts() override;

	com_unique_ptr<ID3D11Buffer> m_psCB;
};

}
