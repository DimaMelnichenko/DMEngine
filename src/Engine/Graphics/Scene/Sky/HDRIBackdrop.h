#pragma once

#include <string>
#include "SceneObject.h"
#include "SkyLight.h"
#include "Shaders\FullscreenShader.h"
#include "D3D\CubeTarget.h"
#include "Properties\PropertyContainer.h"

namespace GS
{

// HDRI-панорама — небо уровня вместо процедурной атмосферы (как HDRI Backdrop в UE): фон и освещение окружением.
// Строка таблицы HDRIBackdrop по ссылке Levels.hdri_backdrop. Файл — равнопромежуточная проекция (.hdr, .exr, .dds;
// Shaders/hdri.sh) — грузится один раз как R32G32B32A32_FLOAT с мипами: солнце в панораме бывает ярче предела half.
// compute() при смене поворота или среза строит из неё cubemap (Shaders/hdri_cube.ps) и отдаёт его SkyLight; фон —
// свой вызов в проходе sky (Shaders/hdri_background.ps). Яркость = значение панорамы × intensity (cb_skyLightScale).
// Воздушной перспективы и пропускания к солнцу у уровня с панорамой нет. Сводка по панораме — в логе при загрузке:
// по ней подбираются intensity, срез и направление солнца. Окно GUI «HDRI backdrop»
class HDRIBackdrop : public SceneObject
{
public:
	// Строка HDRIBackdrop
	struct Settings
	{
		std::string texture;		// файл от Textures\: .hdr, .exr, .dds
		float intensity = 1.0f;		// кд/м² на единицу значения панорамы (Intensity в UE)
		float rotation = 0.0f;		// поворот панорамы вокруг вертикали, градусы
		float maxLuminance = 0.0f;	// срез яркости для освещения окружением, в единицах панорамы; 0 — без среза
	};

	HDRIBackdrop();

	// false — файл не найден или не читается: уровень тогда освещает атмосфера
	bool initialize( const Settings& settings, SkyLight& skyLight );
	// Текущие значения из GUI — для сохранения уровня
	Settings settings();
	// Масштаб яркости фона и освещения окружением (cb_skyLightScale)
	float intensity();
	// Фон не рисуется, если у уровня своя модель неба (SkySphere); освещение окружением остаётся
	void setBackgroundVisible( bool visible );

	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	PropertyContainer* properties() override;

private:
	// Константный буфер PS b2, раскладка как у HDRIParameters в Shaders/hdri.sh
	struct alignas( 16 ) Parameters
	{
		int32_t face;
		float rotation;		// радианы
		float maxLuminance;
		float lod;
	};

	// rotation — поворот панорамы, градусы: для направления на солнце в сводке
	bool loadPanorama( const std::string& path, float rotation );
	Parameters currentParameters();
	void setParameters( const Parameters& params );
	void updateEnvironment( const Parameters& params );

	SkyLight* m_skyLight = nullptr;
	std::string m_texture;
	PropertyContainer m_properties;
	bool m_backgroundVisible = true;
	bool m_environmentValid = false;
	Parameters m_computedFor = {};

	FullscreenShader m_cubeShader;
	FullscreenShader m_backgroundShader;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	com_unique_ptr<ID3D11ShaderResourceView> m_panorama;
	uint32_t m_panoramaWidth = 0;
	CubeTarget m_cube;	// источник SkyLight
};

}
