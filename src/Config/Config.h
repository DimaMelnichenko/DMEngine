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
	float backBufferWidth() const
	{
		return m_backBufferWidth;
	}
	float backBufferHeight()  const
	{
		return m_backBufferHeight;
	}
	uint16_t MSAACount() const{ return m_MSAACount; }

private:
	bool m_FullScreen = false;
	bool m_VSync = true;
	float m_ScreenDepth = 2000.0f;
	float m_ScreenNear = 1.0f;
	float m_screenWidth = 1024.0f;
	float m_screenHeight = 576.0f;
	float m_backBufferWidth = 1920.0f;
	float m_backBufferHeight = 1080.0f;
	uint16_t m_MSAACount = 0;

};

