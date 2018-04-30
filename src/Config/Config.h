#pragma once
#include <string>

class Config
{
public:
	Config();
	~Config();

	bool readConfig( const std::string& file );

	bool fullScreen();
	bool vSync();
	float ScreenDepth();
	float ScreenNear();

private:
	bool m_FullScreen = false;
	bool m_VSync = true;
	float m_ScreenDepth = 2000.0f;
	float m_ScreenNear = 1.0f;
};

