#pragma once

#include <string>

// Текстуры хранилища (System::textures) сеткой миниатюр с поиском по имени; наведение — крупнее, с id и размером
class TextureLibraryPanel
{
public:
	void draw( bool* open );

private:
	std::string m_filter;
	float m_thumbnail = 96.0f;
};
