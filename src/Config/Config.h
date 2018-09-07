#pragma once
#include <string>

class Config
{
public:
	Config();
	~Config();

	bool readConfig( const std::string& file );

	bool fullScreen() const		{ return m_FullScreen; }
	bool vSync() const			{ return m_VSync; }
	float ScreenDepth() const	{ return m_ScreenDepth; }
	float ScreenNear() const	{ return m_ScreenNear; }
	float screenWidth() const	{ return m_screenWidth; }
	float screenHeight()  const	{ return m_screenHeight; }
	uint16_t MSAAQuality() const{ return m_MSAAQuality; }

private:
	bool m_FullScreen = false;
	bool m_VSync = true;
	float m_ScreenDepth = 2000.0f;
	float m_ScreenNear = 1.0f;
	float m_screenWidth = 1024.0f;
	float m_screenHeight = 768.0f;
	uint16_t m_MSAAQuality = 0;

};

