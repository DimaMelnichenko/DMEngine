#pragma once

#include <vector>
#include "Storage\DMResource.h"
#include "Properties/PropertyContainer.h"
#include "ShaderProgram.h"
#include "MaterialRenderState.h"

namespace GS
{

// Материал — ресурс хранилища System::materials() (строка Materials в base.db3, класс — колонка class,
// MaterialStorage::createMaterial): программа шейдеров с вариантами и то, как её рисовать с параметрами секции.
// Материал не рисует сам: вызывающий (Renderer::drawMesh, свои вызовы объектов) ставит состояние, setPass( phaseFor ),
// setParams и рисует DMD3D::draw*
class Material : public DMResource, public ShaderProgram
{
public:
	Material( uint32_t id, const std::string& name );
	virtual ~Material();

	// После загрузки шейдеров из базы (LibraryLoader::loadShader): свои варианты шейдеров, фазы, константные буферы
	virtual bool initialize() { return true; }
	// Раскладка вершин материала — до загрузки шейдеров (MaterialStorage::createMaterial)
	virtual std::vector<VertexElement> initLayouts() { return {}; }
	// Параметры материала (MaterialParameterDef) со значениями по умолчанию: копию получает секция модели, значения
	// экземпляра (MaterialParameterInstance) пишутся в неё
	PropertyContainer& parameters() { return m_parameters; }

	// Привязка параметров секции: текстуры, константный буфер материала — после setPass
	virtual void setParams( const PropertyContainer& ) {}
	// Режим и двусторонность материала с этими параметрами (Blend Mode и Two Sided в UE): по ним объект выбирает
	// проход и отсечение граней. По умолчанию — непрозрачный односторонний
	virtual MaterialRenderState renderState( const PropertyContainer& params ) const { return {}; }
	// Фаза (набор шейдеров) для этих параметров — например, вариант с отсечением по альфе — и вызова (options: инстансы,
	// глубина из depth prepass, смена LOD дизерингом): перестановка по ключу permutationKey, −1 — такого варианта у
	// материала нет. Рисуют так: setPass( phaseFor( params ) ), затем setParams( params )
	int phaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const;
	// Есть ли вариант для инстансинга моделей: иначе одинаковые меши рисуются по одному
	virtual bool supportsInstancing() const { return false; }
	// Собрать варианты для экземпляров расстановки (ShaderPhaseOptions::placed) — при загрузке, до прогрева пайплайнов
	// (Renderer::warmPipelines): так модель уровня (ель) годится и для слоя расстановки (лес). false — материал так не умеет
	virtual bool enablePlacedInstances() { return false; }
	// Собрать вариант запекания импостера (ShaderPhaseOptions::impostorBake): цели — цвет и альфа (sRGB), нормаль модели
	// с долей пропускания и глубина поверхности (ImpostorMaterial). false — материал так не умеет, модель с ним в импостер
	// не запекается
	virtual bool enableImpostorBake() { return false; }
	// Собрать варианты скиннинга (ShaderPhaseOptions::skinned) — у материалов секций моделей со скелетом, при загрузке до
	// прогрева пайплайнов. false — материал так не умеет: модель со скелетом рисуется им в позе покоя
	virtual bool enableSkinning() { return false; }
	// Шейдер читает глубину сцены (SLOT_SCENE_DEPTH): после depth prepass такие меши и свои вызовы рисуются проходом
	// opaqueDepthRead — глубина в нём только для чтения, проверка «ближе или равно» (импостер: смещение глубины)
	virtual bool readsSceneDepth() const { return false; }
	// Полупрозрачный шейдер читает цвет сцены за собой (SLOT_SCENE_COLOR: преломление, поглощение воды): перед проходом
	// transparent рендерер копирует HDR-буфер, а глубина в проходе — только для чтения и тоже видна (SLOT_SCENE_DEPTH)
	virtual bool readsSceneColor() const { return false; }
	// Фаза «только глубина» для теней и depth prepass (без пиксельного шейдера или только с отсечением — по альфе,
	// дизерингом смены LOD) или −1: материал тень не отбрасывает и в prepass не рисуется (полупрозрачный или без
	// такой перестановки). Из options важны instanced, placed и lodDither
	int depthPhaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options = {} ) const;
	// Ключ перестановки вызова (биты ShaderPermutation): признаки из options и режима материала с этими параметрами,
	// depth — тени и depth prepass. Экземпляры расстановки — свой вершинный шейдер, инстансный вызов — только вне смены
	// LOD; после depth prepass проход цвета не отсекает (маска и дизеринг уже в глубине, проверка EQUAL)
	virtual uint32_t permutationKey( const PropertyContainer& params, const ShaderPhaseOptions& options, bool depth ) const;

	// Фазы перестановок по видам — для прогрева: «только глубина» (тени, depth prepass) — без цели цвета, запекание
	// импостера — со своими целями (ImpostorMaterial::bake, в прогрев проходов кадра не идёт), остальные рисуют цвет
	std::vector<int> depthPhases() const;
	std::vector<int> bakePhases() const;
	std::vector<int> colorPhases() const;

private:
	// Фазы перестановок, у ключа которых из битов mask стоят ровно bits (без повторов)
	std::vector<int> permutationPhases( uint32_t mask, uint32_t bits ) const;

	PropertyContainer m_parameters;
};

}
