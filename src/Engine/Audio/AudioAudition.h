#pragma once

#include <cstdint>

namespace GS
{

// Прослушивание в редакторе — Mute и Solo у частей звука уровня (подокна «Ambient sound», слои «Soundscape»), как у
// дорожек микшера DAW и au.Debug.SoloSoundClass в UE: не данные уровня, не сохраняются. Mute глушит часть; Solo хоть у
// одной части — звучат только части с Solo. Части разных объектов друг о друге не знают: Solo отмечается в кадре (gain),
// «есть ли Solo» — по прошлому кадру (beginFrame); громкость голоса и так идёт к цели за 50 мс
class AudioAudition
{
public:
	// Начало кадра: Solo, отмеченные за прошлый кадр, — в силе
	void beginFrame()
	{
		m_anySolo = m_soloMarked;
		m_soloMarked = false;
	}
	// Множитель громкости части с флажками mute и solo в этом кадре: 0 или 1. Mute сильнее Solo
	float gain( bool mute, bool solo )
	{
		if( solo )
			m_soloMarked = true;
		if( mute )
			return 0.0f;
		return m_anySolo && !solo ? 0.0f : 1.0f;
	}
	bool anySolo() const { return m_anySolo; }
	// Снять Solo у всех частей (кнопка «Clear solo» окна «Audio»): номер растёт, части сбрасывают свои флажки, увидев новый
	void clearSolo() { ++m_clearRevision; }
	uint32_t clearRevision() const { return m_clearRevision; }

private:
	bool m_anySolo = false;
	bool m_soloMarked = false;
	uint32_t m_clearRevision = 0;
};

}
