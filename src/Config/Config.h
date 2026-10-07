#pragma once
#include <string>
#include "DirectX.h"

class Config
{
public:
	Config();
	~Config();

	bool readConfig( const std::string& file );
	// Параметры командной строки поверх settings.ini: -camera x,y,z[,pitch,yaw], -level имя, -nogui, -nomouse, -remote
	void parseCommandLine( const std::string& commandLine );

	bool fullScreen() const		{ return m_fullScreen; }
	bool vSync() const			{ return m_vSync; }
	float screenDepth() const	{ return m_screenDepth; }
	float screenNear() const	{ return m_screenNear; }
	float screenWidth() const	{ return m_screenWidth; }
	float screenHeight()  const	{ return m_screenHeight; }	
	float backBufferWidth() const
	{
		return m_backBufferWidth;
	}
	float backBufferHeight()  const
	{
		return m_backBufferHeight;
	}
	// Размер среза каскадной карты теней, текселей (ShadowMapResolution) — настройка качества, а не данные уровня
	uint32_t shadowMapResolution() const	{ return m_shadowMapResolution; }
	// Depth prepass (DepthPrepass, как r.EarlyZPass в UE): глубина непрозрачных до прохода цвета. Нет строки — включён
	bool depthPrepass() const				{ return m_depthPrepass; }
	// GPU-based validation debug-слоя D3D12 в Debug-сборке (GpuValidation; медленно). Нет строки — включена
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
	// Удалённое управление по каналу \\.\pipe\DMEngine (-remote, клиент — Tools/engine.py)
	bool remoteControl() const				{ return m_remoteControl; }

private:
	bool m_fullScreen = false;
	bool m_vSync = true;
	float m_screenDepth = 2000.0f;
	float m_screenNear = 1.0f;
	float m_screenWidth = 1024.0f;
	float m_screenHeight = 576.0f;
	float m_backBufferWidth = 1920.0f;
	float m_backBufferHeight = 1080.0f;
	uint32_t m_shadowMapResolution = 2048;
	bool m_depthPrepass = true;
	bool m_gpuValidation = true;
	DirectX::XMFLOAT3 m_cameraPosition = DirectX::XMFLOAT3( 0.0f, 0.0f, -1.0f );
	DirectX::XMFLOAT2 m_cameraRotation = DirectX::XMFLOAT2( 0.0f, 0.0f );
	std::string m_levelName;
	bool m_showGUI = true;
	bool m_mouseLook = true;
	bool m_wind = true;
	bool m_particles = true;
	bool m_remoteControl = false;

};

