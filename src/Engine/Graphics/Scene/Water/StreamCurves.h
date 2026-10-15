#pragma once

#include <memory>
#include <vector>
#include "Level\LevelSettings.h"
#include "Properties\PropertyContainer.h"

namespace GS
{

// Кривые русел в редакторе — данные, по которым конвейер рельефа и воды строит русла (StreamCurve, строки Streams, как
// Landscape Splines в UE): подокна окна воды «Channels» (форма русел, WaterChannelsSettings) и «Streams» (подокно на кривую —
// включение и множители; правка помечает кривую правленной — новая генерация её не заменит), точки кривых (вьюпорт: гизмо,
// Insert / Delete; команда stream) и что правили с последней постройки русел (rebuildRequested — Scene перестраивает русла,
// воду и расстановку без перезагрузки)
class StreamCurves
{
public:
	// Подокна «Channels» и «Streams» — в window (окно воды); кривые — settings.streams
	void initialize( const WaterSimulationSettings& settings, PropertyContainer& window );
	// Форма русел из подокна «Channels»
	WaterChannelsSettings channels() const;
	// С множителями и включением из окна и флагом правки — для сохранения уровня и постройки русел; curves — как есть (точки
	// для вьюпорта)
	std::vector<StreamCurve> streamCurves() const;
	const std::vector<StreamCurve>& curves() const { return m_curves; }
	// Точка кривой — x, z мира (перетаскивание: русла не перестраиваются до finishCurveEdit)
	void setCurvePoint( size_t curve, size_t point, DirectX::XMFLOAT2 position );
	// Новая точка после after (−1 — в начало: перед первой); удаление — если останется не меньше двух
	bool insertCurvePoint( size_t curve, int after, DirectX::XMFLOAT2 position );
	bool removeCurvePoint( size_t curve, size_t point );
	// Правка кривой закончена (отпустили гизмо, вставили или удалили точку): кривая правленная, русла — перестроить
	void finishCurveEdit( size_t curve );
	// Подокно кривой в «Streams» — для выбора в Details
	PropertyContainer* curveProperties( size_t curve );
	// Русла надо перестроить: правили кривые, множители ручьёв или форму русел в «Channels» (параметры генератора кривых —
	// при следующей загрузке)
	bool rebuildRequested() const;
	// Русла построены по нынешним кривым и форме
	void markBuilt();

private:
	PropertyContainer m_channelsProperties;	// «Channels»: WaterChannelsSettings
	// «Streams»: кривые русел — включение и множители, подокно на кривую (порядок — как m_curves)
	PropertyContainer m_streamsProperties;
	std::vector<std::unique_ptr<PropertyContainer>> m_streamProperties;
	WaterChannelsSettings m_initialChannels;
	// Кривые русел: нынешние (точки правит вьюпорт) и как построены — по ним видно, что множители, включение и форма русел
	// изменились (rebuildRequested)
	std::vector<StreamCurve> m_curves;
	std::vector<StreamCurve> m_builtCurves;
	WaterChannelsSettings m_builtChannels;
	bool m_curvesDirty = false;
	// Номер правки подокон, по которому сверены кривые и форма русел (rebuildRequested): не сменился — сверять нечего
	mutable uint64_t m_checkedRevision = ~0ull;
};

}
