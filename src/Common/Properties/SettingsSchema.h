#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <variant>
#include <vector>
#include <DirectXMath.h>

// Описание полей настроек — как UPROPERTY в UE: одно место на параметр. Поле — колонка базы, свойство окна, доступ к члену
// структуры, границы, единицы и подсказка; по описанию строятся свойства окна и структура из них
// (Properties/SettingsProperties.h), чтение строки базы и запись (ObjectLibrary/SettingsDatabase.h). Описания — рядом с
// данными (Scene/Level/LevelSettings.cpp: volumetricCloudSchema и др.), образец — terrainLayerRuleFields

enum SettingFlag : uint32_t
{
	settingLogarithmic = 1u << 0,	// ползунок по логарифму (шаг, размер)
	settingColor = 1u << 1,			// XMFLOAT3 / XMFLOAT4 — выбор цвета
	settingClamp = 1u << 2,			// из окна — в границы low…high (поле ввода их не держит)
	settingClampLow = 1u << 3,		// из окна — не меньше low (сверху — сколько ввели)
	settingTransient = 1u << 4,		// правка не делает уровень изменённым (Property::setTransient)
};

template<typename T>
struct SettingField
{
	// Доступ к члену структуры (и вложенному — у тумана слои): лямбда без захвата, DM_SETTING
	using Access = std::variant<float& (*)( T& ), bool& (*)( T& ), int32_t& (*)( T& ), uint32_t& (*)( T& ),
								DirectX::XMFLOAT2& (*)( T& ), DirectX::XMFLOAT3& (*)( T& ), DirectX::XMFLOAT4& (*)( T& ),
								std::string& (*)( T& )>;

	// Колонка базы; у вектора — колонки компонент через запятую («rotation_x,rotation_y,rotation_z») или одна текстом
	// «x,y,z»; NULL в базе — значение по умолчанию структуры, пустая строка — NULL; nullptr — поле не хранится
	const char* column = nullptr;
	const char* name = nullptr;		// свойство окна (без «/» — разделителя пути set); nullptr — только в базе
	Access access;
	float low = 0.0f;				// границы контрола; low < high — с ними (верхняя в окне — не меньше значения)
	float high = 0.0f;
	const char* unit = "";
	const char* tooltip = "";
	uint32_t flags = 0;				// SettingFlag
	// Часть окна, которую объект добавляет по условию (импостер слоя расстановки и т. п.); "" — основная
	const char* group = "";
	// Целое с выбором из значений (размер кадра импостера): в окне — список, свойство — номер значения
	std::vector<int32_t> choices = {};
};

template<typename T>
using SettingsSchema = std::vector<SettingField<T>>;

// Поле по колонке базы; nullptr — нет
template<typename T>
const SettingField<T>* findSettingField( const SettingsSchema<T>& schema, const char* column )
{
	for( const SettingField<T>& field : schema )
		if( field.column && std::string( field.column ) == column )
			return &field;
	return nullptr;
}

// Номер ближайшего значения из выбора поля
inline int32_t nearestSettingChoice( const std::vector<int32_t>& choices, int32_t value )
{
	int32_t index = 0;
	for( int32_t i = 0; i < static_cast<int32_t>( choices.size() ); ++i )
		if( std::abs( choices[i] - value ) < std::abs( choices[index] - value ) )
			index = i;
	return index;
}

// Доступ к члену s.member структуры Settings (имя типа в области, где описание): поле таблицы описания
#define DM_SETTING( member ) []( Settings& s ) -> auto& { return s.member; }
