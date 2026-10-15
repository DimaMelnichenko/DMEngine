#pragma once

#include "Material.h"
#include "RenderView.h"
#include "Shaders\impostor_constants.h"

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
	static constexpr uint32_t defaultFrameSize = 128;	// кадр, текселей (BakeSettings::frameSize)
	// Сверхвыборка запекания: кадр рисуется со стороной frameSize × bakeSamples и усредняется — альфа становится долей
	// покрытия, а не попаданием тонкой хвоинки в центр текселя
	static constexpr uint32_t bakeSamples = 4;
	// Кадры — в кэше на диске (cache\impostors\<debug|release>\<модель>\<ключ>_color|normal|offset.dds), как DDC в UE: ключ — вершины и
	// индексы мешей секций LOD0, их материалы (параметры секций, отпечатки файлов текстур, шейдеры варианта запекания —
	// файл, точка входа, defines: ShaderProgram::phaseSources), настройки запекания, отпечаток Shaders\ и сборка (Debug и
	// Release — свои кадры: оптимизация шейдеров меняет округление). Меняется код запекания, которого нет в ключе (обработка
	// кадров на CPU), — поднять версию. Пустые кадры (вариант запекания ничего не нарисовал) в кэш не пишутся, пустой кэш
	// запекается заново — строка в лог
	static constexpr uint32_t cacheVersion = 2;

	// Настройки запекания (ScatterLayers.impostor_density / impostor_occlusion / impostor_frame_size)
	struct BakeSettings
	{
		float density = 1.0f;	// множитель доли покрытия: > 1 — крона плотнее
		float occlusion = 0.0f;	// сила затенения окружающего света внутри кроны, 0…1
		// Сторона кадра, текселей (степень двойки 16…512): пучку травы вдали хватит 64 — вчетверо меньше памяти и запекания
		uint32_t frameSize = defaultFrameSize;
	};

	// flat — плоский импостер (у слоя импостер без тени, трава): глубина — самой карточки, без смещения глубины и без
	// чтения глубины сцены — после prepass обычный проход opaque; у травы до горизонта сотни карточек на пиксель, и
	// глубина из шейдера с проверкой по глубине сцены стоила дороже мешей, которые импостер заменил
	ImpostorMaterial( uint32_t id, const std::string& name, bool flat = false );

	bool initialize() override;
	std::vector<VertexElement> initLayouts() override;
	// Запекание модели: материалы её секций — с вариантом запекания (Material::enableImpostorBake). false — не вышло (лог)
	bool bake( DMModel& model, const BakeContext& context, const BakeSettings& settings = {} );

	void setParams( const PropertyContainer& ) override;
	// Затемнение низа (ScatterLayers.impostor_base_shade, свойство слоя «Impostor base shade»): цвет у корня × (1 − shade),
	// к верху модели — без затемнения. Импостер без тени (трава вдали) так не выглядит светлее травы с тенями рядом.
	// Самотень кроны (impostor_self_shadow, «Impostor self shadow»): сила тени солнца от самой модели по её кадрам, 0…1
	// (Shaders/impostor.ps; у плоского импостера её нет). Буфер констант — при смене
	void setShading( float baseShade, float selfShadow );
	// Masked без отсечения граней (карточка всегда к виду), со сменой LOD дизерингом
	MaterialRenderState renderState( const PropertyContainer& params ) const override;
	// Экземпляры — всегда из пула; после depth prepass (проход opaqueDepthRead — видимость по глубине сцены) дизеринг
	// смены LOD отсекается, как в prepass
	uint32_t permutationKey( const PropertyContainer& params, const ShaderPhaseOptions& options, bool depth ) const override;
	// Экземпляры — всегда из пула расстановки
	bool enablePlacedInstances() override { return true; }
	// Глубина пикселя — запечённой поверхности: после depth prepass видимость — по глубине сцены (проход opaqueDepthRead)
	bool readsSceneDepth() const override { return !m_flat; }

private:

	// Вид кадра запекания frame (номер по сторонам сетки): ортография на сферу модели со стороны его направления
	RenderView frameView( uint32_t x, uint32_t y ) const;

	// Шейдеры перестановки key (компилируются, если таких ещё нет) и её фаза: вершинный — экземпляр расстановки, с
	// LOD_DITHER при смене LOD; пиксельный цвета — без depth prepass с отсечением по покрытию и глубиной поверхности
	// (ALPHA_MASK), после него — видимость по глубине сцены; «только глубина» — mainDepth (prepass и тени)
	bool createPermutation( uint32_t key );

	bool m_flat = false;
	ImpostorBuffer m_params = {};
	uint32_t m_colorTexture = 0;
	uint32_t m_normalTexture = 0;
	uint32_t m_offsetTexture = 0;	// глубина поверхности кадров (смещение глубины) и затенение окружающего света, R16G16
	Buffer m_constantBuffer;
};

}
