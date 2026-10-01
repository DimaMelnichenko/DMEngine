#pragma once

#include "Material.h"
#include "RenderView.h"

namespace GS
{

class DMModel;
struct BakeContext;

// Импостер модели — как octahedral impostors в UE (ImpostorBaker): при загрузке модель (LOD0, все секции) рисуется
// с frames² направлений верхней полусферы (сетка на полуоктаэдре, Shaders/impostor.sh) в массивы текстур — цвет и
// покрытие, нормаль модели и доля пропускания, глубина поверхности, — с мипами, сохраняющими покрытие хвои. В кадре —
// карточка к виду с тремя ближайшими кадрами (Shaders/impostor.vs / .ps), освещённая как материалы в точке запечённой
// поверхности; глубина пикселя — тоже её (pixel depth offset): дерево не уходит в склон, крона затеняет себя. Это
// последний LOD варианта слоя расстановки (Scatterer: ScatterLayers.impostor_distance): экземпляры из пула расстановки,
// смена LOD — дизерингом. Материал — в хранилище System::materials() (прогрев пайплайнов — как у остальных), текстуры —
// в System::textures()
class ImpostorMaterial : public Material
{
public:
	static constexpr uint32_t frames = 8;		// кадров по стороне сетки (g_impostorFrames в Shaders/impostor.sh)
	static constexpr uint32_t frameSize = 128;	// кадр, текселей

	ImpostorMaterial( uint32_t id, const std::string& name );

	bool initialize() override;
	std::vector<VertexElement> initLayouts() override;
	// Запекание модели: материалы её секций — с вариантом запекания (Material::enableImpostorBake). false — не вышло (лог)
	bool bake( DMModel& model, const BakeContext& context );

	void setParams( const PropertyContainer& ) override;
	// Masked без отсечения граней (карточка всегда к виду), со сменой LOD дизерингом
	MaterialRenderState renderState( const PropertyContainer& params ) const override;
	int phaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const override;
	int depthPhaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const override;
	std::vector<int> depthPhases() const override;
	// Экземпляры — всегда из пула расстановки
	bool enablePlacedInstances() override { return true; }
	// Глубина пикселя — запечённой поверхности: после depth prepass видимость — по глубине сцены (проход opaqueDepthRead)
	bool readsSceneDepth() const override { return true; }

private:
	// cbuffer ImpostorBuffer в Shaders/impostor.sh
	struct alignas( 16 ) Params
	{
		DirectX::XMFLOAT4 bounds;			// сфера модели: центр в её координатах, радиус
		DirectX::XMFLOAT4 extents;			// половины ящика LOD0 (центр — центр сферы)
		DirectX::XMFLOAT4 transmission;		// цвет пропускания (множитель альбедо), множитель доли
		uint32_t frames;
		float alphaCutoff;
		float roughness;
		float padding;
	};
	static_assert( sizeof( Params ) == 64, "ImpostorBuffer layout" );

	// Вид кадра запекания frame (номер по сторонам сетки): ортография на сферу модели со стороны его направления
	RenderView frameView( uint32_t x, uint32_t y ) const;

	// Фазы: цвет — [вершинный с LOD_DITHER][без depth prepass: ALPHA_MASK и глубина поверхности; иначе — видимость по
	// глубине сцены][отсечение дизерингом], глубина — [LOD_DITHER]
	int m_colorPhases[2][2][2] = {};
	int m_depthPhases[2] = {};
	Params m_params = {};
	uint32_t m_colorTexture = 0;
	uint32_t m_normalTexture = 0;
	uint32_t m_offsetTexture = 0;	// глубина поверхности кадров (смещение глубины, R16)
	Buffer m_constantBuffer;
};

}
