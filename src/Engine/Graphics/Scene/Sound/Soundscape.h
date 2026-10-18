#pragma once

#include <memory>
#include <random>
#include <string>
#include <vector>
#include "SceneObject.h"
#include "Level\LevelSettings.h"
#include "Engine\Audio\AudioClip.h"
#include "Engine\Audio\AudioMixer.h"
#include "SoundDebug.h"

namespace GS
{

class TerrainPipeline;
class WorldClock;

// Певцы уровня — как Soundscape в UE (docs/audio.md, «Певцы»): слои SoundscapeLayers, у слоя — записи (строки Sounds, их фразы
// собрал Tools/pack_sound.py; у каждого певца — одна из них) и правила. Певец садится вокруг слушателя — в полосе расстояний слоя, на высоте над землёй, там,
// где есть его место по маскам «habitat» (до 16 попыток; «lake_shore» — на берегу озера: точки края воды озёр из растра
// статичной воды, TerrainHydrology::Result::staticWater), — поёт фразы записи с паузами весь свой «bout», потом улетает, и
// появляется другой. Певцов сразу — «singers» × активность слоя: произведение его правил (SoundRules.h — часы суток по мировым
// часам WorldClock и т. д.). Голос — на фразу
// (ClipGenerator), у певца своя высота; громкость — слоя × записи, по расстоянию от 10 м, поглощение воздухом и заглушение
// гребнем (SoundPlace.h), как у ручьёв.
// Окно «Soundscape» (раздел Audio) — подокно на слой: «Enabled», поля слоя, записи и места флажками (подокна «Sounds» и
// «Habitat»), правила (подокно «Rules»: подокно на правило с полями по описанию типа, «Add … rule», «Delete»), прослушивание
// «Mute» / «Solo» (AudioAudition), «Delete»; кнопка окна «Add layer» — новый слой (как источники
// света: Ctrl+Z, «Save level» вставляет и удаляет строки). Окно «Sounds» — записи: громкость (Sounds.volume), «Play» / «Stop» —
// послушать фразы записи у слушателя. Флажок видимости в Outliner — тихо
class Soundscape : public SceneObject
{
public:
	using Layer = SoundscapeLayerSettings;
	// Место певца «на берегу озера» в habitat (вместо масок)
	static constexpr const char* lakeShore = "lake_shore";

	Soundscape();
	~Soundscape() override;
	// sounds — все записи (строки Sounds): звук записи читается, когда её поёт слой или слушают; нет файла — строка в лог, запись
	// молчит; clock — время суток (мировые часы)
	bool initialize( const std::vector<Layer>& layers, const std::vector<SoundRecord>& sounds, AudioMixer& mixer,
					 const TerrainPipeline* terrain, const WorldClock& clock );
	bool initialized() const { return m_mixer != nullptr; }

	void update( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override {}
	PropertyContainer* properties() override { return &m_properties; }
	bool savedWithLevel() const override { return true; }
	bool audio() const override { return true; }
	// Окно «Sounds»: подокно на запись
	PropertyContainer* soundProperties() { return &m_soundProperties; }

	// «Save level»: слои окна по порядку (id 0 — добавлен в редакторе) с правилами, строки убранных слоёв и правил (правила
	// убранного слоя уходят с ним), записи с громкостью окна; после записи — id новых строк в слои и правила (saved — те же слои
	// и правила по порядку)
	std::vector<Layer> layerSettings() const;
	std::vector<uint32_t> removedIds() const;
	std::vector<uint32_t> removedRuleIds() const;
	std::vector<SoundRecord> soundRecords() const;
	void markSaved( const std::vector<Layer>& saved );

	// Новый слой (окно, кнопка «Add layer»): поля по умолчанию, без записи — молчит; ключ — для удаления и отмены
	uint32_t addLayer();
	// Убрать слой (певцы умолкают) и вернуть убранный (отмена); false — нет такого
	bool removeLayer( uint32_t key );
	bool restoreLayer( uint32_t key );

	// Метки режима «Sound sources»: певцы (цвет слоя, ярче — пока звучит фраза), области слоёв с «Show area» — полоса
	// расстояний, маски места, берег озера
	void soundDebug( SoundDebug& out );

private:
	// Правило слоя: строка SoundRules и подокно с полями по описанию типа (SoundRuleType)
	struct RuleState
	{
		uint32_t key = 0;			// номер правила в слое — для удаления и отмены
		SoundRule rule;				// загруженное (или с прошлого сохранения): id, тип
		PropertyContainer properties;
	};
	// Запись: строка Sounds, подокно окна «Sounds», звук (читается при первой нужде) и прослушивание кнопкой «Play»
	struct Sound
	{
		SoundRecord record;
		PropertyContainer properties;
		std::shared_ptr<const AudioClip> clip;
		bool loaded = false;			// чтение было (clip пуст — не вышло)
		AudioMixer::VoiceId preview = 0;
		float previewLeft = 0.0f;		// с: столько ещё звучит фраза «Play»
		size_t previewPhrase = 0;		// следующая фраза «Play»
	};
	struct Singer
	{
		DirectX::XMFLOAT3 position;
		float pitch = 1.0f;
		float boutLeft = 0.0f;		// с: столько ещё поёт с этого места
		float nextPhrase = 0.0f;	// с: до начала следующей фразы (от начала прошлой — её длина и пауза)
		// Звучащие фразы: голос и сколько ему ещё, с. Две — у фраз внахлёст (пауза меньше нуля: хор без шва)
		struct Phrase
		{
			AudioMixer::VoiceId voice = 0;
			float left = 0.0f;
		};
		std::vector<Phrase> phrases;
		size_t sound = 0;			// запись, которую поёт этот певец (номер в m_sounds)
	};
	struct LayerState
	{
		uint32_t key = 0;			// номер слоя в окне — для удаления и отмены
		Layer settings;				// загруженные (или с прошлого сохранения) — окно правит копию (current)
		PropertyContainer properties;
		PropertyContainer soundFlags;	// подокно «Sounds»: флажок на запись
		PropertyContainer habitatFlags;	// подокно «Habitat»: флажок на маску и берег озера
		PropertyContainer ruleProperties;	// подокно «Rules»: подокно на правило, кнопки «Add … rule»
		std::vector<std::unique_ptr<RuleState>> rules;
		std::vector<std::unique_ptr<RuleState>> removedRules;	// убранные в редакторе: вернёт отмена
		uint32_t nextRuleKey = 1;
		// Флажки текстом и записи со звуком — заново, когда флажки правили (номер правки)
		uint64_t listsRevision = ~0ull;
		std::string sound;
		std::string habitat;
		std::vector<size_t> sounds;		// номера в m_sounds
		std::vector<Singer> singers;
		float spawnDelay = 0.0f;	// с: до следующего певца — идёт, пока место пустует («Arrival»)
	};

	// Слой с правками окна: поля, записи и места — текстом из флажков, правила — из их подокон
	Layer current( const LayerState& layer ) const;
	std::vector<SoundRule> currentRules( const LayerState& layer ) const;
	// Правило слоя: подокно с полями типа и «Delete»; добавить (значения по умолчанию), убрать и вернуть (отмена); подокна
	// «Rules» — по списку (новые и убранные отмечают окно изменённым до «Save level»)
	std::unique_ptr<RuleState> createRule( LayerState& layer, const SoundRuleType& type, const SoundRule& rule );
	uint32_t addRule( LayerState& layer, const SoundRuleType& type );
	bool removeRule( LayerState& layer, uint32_t key );
	bool restoreRule( LayerState& layer, uint32_t key );
	void updateRules( LayerState& layer );
	// Списки слоя по флажкам (LayerState::sound, habitat, sounds) — если флажки правили
	void refreshLists( LayerState& layer );
	// Окно слоя: поля, флажки, прослушивание, «Delete»
	std::unique_ptr<LayerState> createLayer( const Layer& settings );
	// Подокна слоёв окна «Soundscape» — по списку; новые и убранные слои отмечают окно изменённым до «Save level»
	void updateStructure();
	// Певцы слоя умолкают и улетают сразу (слой убран)
	void silence( LayerState& layer );
	// Звук записи; nullptr — не читается или фраз нет
	const AudioClip* clipOf( Sound& sound );
	// Записи слоя — номера в m_sounds по флажкам, у которых есть звук
	std::vector<size_t> layerSounds( const LayerState& layer );
	// Место нового певца; false — места по маскам рядом нет
	bool placeSinger( const Layer& settings, const DirectX::XMFLOAT3& listener, Singer& singer );
	float uniform( float from, float to );
	// Берег озёр: клетки озера, у которых сосед — не озеро; высота — уровень озера. Заново — по номеру итоговой карты
	void buildShore();
	// «Play» записи: фразы по очереди у слушателя (громкость записи); «Stop» — затихает
	void play( Sound& sound );
	void stop( Sound& sound );
	void updatePreviews( float step, const AudioListener& listener );

	PropertyContainer m_properties;
	PropertyContainer m_soundProperties;	// окно «Sounds»
	std::vector<std::unique_ptr<Sound>> m_sounds;
	std::vector<std::unique_ptr<LayerState>> m_layers;
	std::vector<std::unique_ptr<LayerState>> m_removed;	// убранные в редакторе: вернёт отмена
	std::vector<std::string> m_habitats;	// флажки мест: маски рельефа и берег озера
	uint32_t m_nextKey = 1;
	uint32_t m_clearSolo = 0;				// номер «Clear solo», по который сняты Solo слоёв
	AudioMixer* m_mixer = nullptr;
	const TerrainPipeline* m_terrain = nullptr;
	const WorldClock* m_clock = nullptr;
	std::mt19937 m_random{ 0x5EEDu };
	std::vector<DirectX::XMFLOAT3> m_shore;
	uint32_t m_shoreRevision = ~0u;
};

}
