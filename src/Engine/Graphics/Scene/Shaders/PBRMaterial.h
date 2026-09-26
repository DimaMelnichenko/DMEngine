#pragma once

#include "DMShader.h"

namespace GS
{

// Материал PBR metallic/roughness (как Default Lit в UE5 и материал glTF 2.0), шейдер Shaders/PBRLit.ps.
// Параметры названы как в glTF: текстуры BaseColor, MetallicRoughness (G — roughness, B — metallic), Normal,
// Occlusion (R), Emissive и множители к ним. Текстура 0 — «не задана»: вместо неё белая или плоская нормаль
class PBRMaterial : public DMShader
{
public:
	PBRMaterial();
	~PBRMaterial();
	void setParams( const PropertyContainer& ) override;

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
	};

	bool innerInitialize() override;
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts() override;

	com_unique_ptr<ID3D11Buffer> m_psCB;
};

}
