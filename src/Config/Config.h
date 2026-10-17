#pragma once
#include <string>
#include "DirectX.h"

class Config
{
public:
	Config();
	~Config();

	bool readConfig( const std::string& file );
	// Параметры командной строки поверх settings.ini: -camera x,y,z[,pitch,yaw], -level имя, -resolution desktop|WxH,
	// -nogui, -nomouse, -remote
	void parseCommandLine( const std::string& commandLine );

	bool vSync() const			{ return m_vSync; }
	float screenDepth() const	{ return m_screenDepth; }
	float screenNear() const	{ return m_screenNear; }
	// Окно и задний буфер (Resolution в settings.ini, -resolution): desktop — окно без рамки на весь основной монитор в его
	// разрешении; W×H — окно с заголовком и клиентской областью W×H (физические пиксели). Задний буфер — клиентская область
	bool desktopResolution() const	{ return m_windowWidth == 0 || m_windowHeight == 0; }
	uint32_t windowWidth() const	{ return m_windowWidth; }
	uint32_t windowHeight() const	{ return m_windowHeight; }
	// Размер среза каскадной карты теней, текселей (ShadowMapResolution) — настройка качества, а не данные уровня
	uint32_t shadowMapResolution() const	{ return m_shadowMapResolution; }
	// Depth prepass (DepthPrepass, как r.EarlyZPass в UE): глубина непрозрачных до прохода цвета. Нет строки — включён
	bool depthPrepass() const				{ return m_depthPrepass; }
	// GPU-based validation debug-слоя D3D12 в Debug-сборке (GpuValidation=true или -gpuvalidation; медленно — для правок
	// слоя D3D12: ресурсы, барьеры, дескрипторы, проходы). Нет строки — выключена: обычный Debug — только слой
	bool gpuValidation() const				{ return m_gpuValidation; }
	// Закреплённая частота GPU (StableClocks=true или -stableclocks, как «Lock to stable clocks» в PIX): время GPU не зависит
	// от разгона, питания и нагрева — для сравнения замеров (Tools/check_frames.py). Нужен режим разработчика Windows
	bool stableClocks() const				{ return m_stableClocks; }
	// Стартовая камера: положение и поворот (тангаж, рыскание) в градусах, секция [Camera]
	const DirectX::XMFLOAT3& cameraPosition() const	{ return m_cameraPosition; }
	const DirectX::XMFLOAT2& cameraRotation() const	{ return m_cameraRotation; }
	// Уровень из таблицы Levels (секция [Level], Name); пустое имя — первый уровень таблицы
	const std::string& levelName() const	{ return m_levelName; }
	// Пресет окружения уровня (LevelPresets: время суток, небо, постобработка, ветер, туман, облака) — секция [Level],
	// Preset, или -preset имя; пусто — строки самого уровня. Эталон проверок — заблокированный Reference (check_frames.py)
	const std::string& presetName() const	{ return m_presetName; }
	// Показывать окна ImGui; -nogui скрывает их, например для снимков экрана
	bool showGUI() const					{ return m_showGUI; }
	// Камера поворачивается мышью; -nomouse отключает это и скрывает указатель — для снимков с заданной точки
	bool mouseLook() const					{ return m_mouseLook; }
	// -nowind: ветер уровня выключен — растения неподвижны, кадры с одной точки совпадают до пикселя
	bool wind() const						{ return m_wind; }
	// -noparticles: частицы уровня не создаются — кадры с одной точки совпадают до пикселя (частицы движутся)
	bool particles() const					{ return m_particles; }
	// -nocolorgrading: кадр без цветокоррекции уровня, только кривая тонмаппинга — снимки контрольных камер сравнимы с
	// эталоном, как бы её ни настроили (ShowFlag.ColorGrading 0 в UE)
	bool colorGrading() const				{ return m_colorGrading; }
	// -freezetime: время игры стоит на нуле (cb_gameTime: рябь и пена воды, звёзды) — снимки с одной точки совпадают
	// между запусками, хотя загрузка и путь до снимка — разной длины. Время кадра, экспозиция и время суток идут как обычно
	bool gameTimeRunning() const			{ return m_gameTimeRunning; }
	// Удалённое управление по каналу \\.\pipe\DMEngine (-remote, клиент — Tools/engine.py)
	bool remoteControl() const				{ return m_remoteControl; }
	// -pix: захват кадров PIX из самого движка (команда pix), как -attachPIX в UE: захватчик PIX грузится до устройства
	bool pixCapture() const					{ return m_pixCapture; }
	// Звук (секция [Audio]): Enabled=false — звука нет совсем; Volume — громкость вывода 0…1 (окно «Audio»); -mute — вывод
	// молчит, а микшер и запись (audio record) работают: проверки (run.ps1, engine.py, check_frames.py) не шумят
	bool audio() const						{ return m_audio; }
	float audioVolume() const				{ return m_audioVolume; }
	bool audioMuted() const					{ return m_audioMuted; }

private:
	bool m_vSync = true;
	float m_screenDepth = 2000.0f;
	float m_screenNear = 1.0f;
	uint32_t m_windowWidth = 0;		// 0 — desktop
	uint32_t m_windowHeight = 0;
	uint32_t m_shadowMapResolution = 2048;
	bool m_depthPrepass = true;
	bool m_gpuValidation = false;
	bool m_stableClocks = false;
	DirectX::XMFLOAT3 m_cameraPosition = DirectX::XMFLOAT3( 0.0f, 0.0f, -1.0f );
	DirectX::XMFLOAT2 m_cameraRotation = DirectX::XMFLOAT2( 0.0f, 0.0f );
	std::string m_levelName;
	std::string m_presetName;
	bool m_showGUI = true;
	bool m_pixCapture = false;
	bool m_mouseLook = true;
	bool m_wind = true;
	bool m_particles = true;
	bool m_colorGrading = true;
	bool m_gameTimeRunning = true;
	bool m_remoteControl = false;
	bool m_audio = true;
	float m_audioVolume = 1.0f;
	bool m_audioMuted = false;

};

