#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace GS
{

// Правила звука — когда и как часто звучит слой певцов (docs/audio.md, «Правила»), как условия у Soundscape в UE: правило — тип
// и параметры (строка SoundRules: type, enabled, params «ключ=значение;…»), активность слоя 0…1 — произведение его включённых
// правил (правил нет — 1); певцов сразу — «Singers» × активность. Тип правила — строка таблицы soundRuleTypes (SoundRules.cpp):
// имя, подпись окна, параметры с границами и единицами, оценка по входу кадра. Новое правило — строка таблицы (и поле
// SoundRuleContext, если ему нужен новый вход); окно «Soundscape», база и Tools/soundscape.py берут его оттуда сами

// Вход правил: что знает мир в кадре
struct SoundRuleContext
{
	float hour = 12.0f;		// час суток мировых часов, 0…24 (WorldClock)
	float windSpeed = 0.0f;	// ветер уровня — погода, м/с (Wind.speed; ветер выключен — 0)
};

// Параметр типа правила: ключ в params, подпись в окне, значение по умолчанию, границы поля, единицы, подсказка
struct SoundRuleParam
{
	const char* key;
	const char* label;
	float defaultValue;
	float low;
	float high;
	const char* unit;
	const char* tooltip;
};

// Тип правила: имя в базе, подпись окна и кнопки «Add …», подсказка, параметры по порядку, оценка — множитель активности
// 0…1 по значениям параметров (по порядку params) и входу кадра
struct SoundRuleType
{
	const char* name;
	const char* label;
	const char* tooltip;
	std::vector<SoundRuleParam> params;
	float ( *evaluate )( const float* values, const SoundRuleContext& context );
};

// Все типы правил; тип по имени в базе (nullptr — нет такого)
const std::vector<SoundRuleType>& soundRuleTypes();
const SoundRuleType* soundRuleType( const std::string& name );

// Правило слоя — строка SoundRules
struct SoundRule
{
	uint32_t id = 0;			// строка SoundRules; 0 — добавлено в редакторе
	std::string type;			// SoundRuleType::name
	bool enabled = true;
	std::vector<float> values;	// по параметрам типа
};

// Значения параметров из текста «ключ=значение;…»: нет ключа или не число — значение по умолчанию, лишние ключи —
// пропускаются; и обратно — текстом по порядку параметров
std::vector<float> parseSoundRuleValues( const SoundRuleType& type, const std::string& text );
std::string formatSoundRuleValues( const SoundRuleType& type, const std::vector<float>& values );

// Множитель одного правила 0…1: выключенное и неизвестного типа — 1
float soundRuleFactor( const SoundRule& rule, const SoundRuleContext& context );
// Активность — произведение правил
float soundActivity( const std::vector<SoundRule>& rules, const SoundRuleContext& context );

// Окно часов: 1 в часах from…to (from > to — через полночь), у краёв — плавно за fade часов, на самой границе — половина;
// правило «Hours»
float hoursActivity( float from, float to, float fade, float hour );
// Ветер: 0 при скорости from, 1 при to, между — плавно (smoothstep); from > to — наоборот, поёт в тихую погоду и затихает в
// сильный ветер; правило «Wind»
float windActivity( float from, float to, float speed );

}
