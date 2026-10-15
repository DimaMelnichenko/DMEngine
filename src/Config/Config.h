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
	// Стартовая камера: положение и поворот (тангаж, рыскание) в градусах, секция [Camera]
	const DirectX::XMFLOAT3& cameraPosition() const	{ return m_cameraPosition; }
	const DirectX::XMFLOAT2& cameraRotation() const	{ return m_cameraRotation; }
	// Уровень из таблицы Levels (секция [Level], Name); пустое имя — первый уровень таблицы
	const std::string& levelName() const	{ return m_levelName; }
	// Показывать окна ImGui; -nogui скрывает их, например для снимков экрана
	bool showGUI() const					{ return m_showGUI; }
	// Камера поворачивается мышью; -nomouse отключает это и скрывает указатель — для снимков с заданной точки
	bool mouseLook() const					{ return m_mouseLook; }
	// -nowind: ветер уровня выключен — растения неподвижны, кадры с одной точки совпадают до пикселя
	bool wind() const						{ return m_wind; }
	// -noparticles: частицы уровня не создаются — кадры с одной точки совпадают до пикселя (частицы движутся)
	bool particles() const					{ return m_particles; }
	// -freezetime: время игры стоит на нуле (cb_gameTime: рябь и пена воды, звёзды) — снимки с одной точки совпадают
	// между запусками, хотя загрузка и путь до снимка — разной длины. Время кадра, экспозиция и время суток идут как обычно
	bool gameTimeRunning() const			{ return m_gameTimeRunning; }
	// Удалённое управление по каналу \\.\pipe\DMEngine (-remote, клиент — Tools/engine.py)
	bool remoteControl() const				{ return m_remoteControl; }
	// -pix: захват кадров PIX из самого движка (команда pix), как -attachPIX в UE: захватчик PIX грузится до устройства
	bool pixCapture() const					{ return m_pixCapture; }

private:
	bool m_vSync = true;
	float m_screenDepth = 2000.0f;
	float m_screenNear = 1.0f;
	uint32_t m_windowWidth = 0;		// 0 — desktop
	uint32_t m_windowHeight = 0;
	uint32_t m_shadowMapResolution = 2048;
	bool m_depthPrepass = true;
	bool m_gpuValidation = false;
	DirectX::XMFLOAT3 m_cameraPosition = DirectX::XMFLOAT3( 0.0f, 0.0f, -1.0f );
	DirectX::XMFLOAT2 m_cameraRotation = DirectX::XMFLOAT2( 0.0f, 0.0f );
	std::string m_levelName;
	bool m_showGUI = true;
	bool m_pixCapture = false;
	bool m_mouseLook = true;
	bool m_wind = true;
	bool m_particles = true;
	bool m_gameTimeRunning = true;
	bool m_remoteControl = false;

};

