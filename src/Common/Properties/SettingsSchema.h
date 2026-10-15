#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
#include <DirectXMath.h>

// Описание полей настроек — как UPROPERTY в UE: одно место на параметр. Поле — колонка базы, свойство окна, доступ к члену
// структуры, границы, единицы и подсказка; по описанию строятся свойства окна и структура из них
// (Properties/SettingsProperties.h), чтение строки базы и запись (ObjectLibrary/SettingsDatabase.h). Описания — рядом с
// данными (Scene/Level/LevelSettings.cpp: volumetricCloudSchema и др.)

enum SettingFlag : uint32_t
{
	settingLogarithmic = 1u << 0,	// ползунок по логарифму (шаг, размер)
	settingColor = 1u << 1,			// XMFLOAT3 / XMFLOAT4 — выбор цвета
	settingClamp = 1u << 2,			// из окна — в границы low…high (поле ввода их не держит)
	settingClampLow = 1u << 3,		// из окна — не меньше low (сверху — сколько ввели)
	settingTransient = 1u << 4,		// правка не делает уровень изменённым (Property::setTransient)
	settingDrag = 1u << 5,			// поле без заливки доли (GUIControlType::DRAG): значения на порядки
	settingRangeControl = 1u << 6,	// полоса «от — до» одним двойным полем (GUIControlType::RANGE): конец не уходит за начало
	settingWideHigh = 1u << 7,		// верхняя граница в окне — не меньше 4 × значения: у величин без естественного предела
									// (частота, размер частиц) ползунку есть куда расти
};

// Полоса «от — до» (правила слоёв: уклон, высота …): в окне — пара полей XMFLOAT2, в базе — две колонки
struct SettingRange
{
	float from;
	float to;
};

template<typename T>
struct SettingField
{
	// Доступ к члену структуры: указатель на член (&Settings::density — и в таблицах, построенных в цикле) или лямбда без
	// захвата (вложенный член — у тумана слои), DM_SETTING
	using Access = std::variant<float& (*)( T& ), bool& (*)( T& ), int32_t& (*)( T& ), uint32_t& (*)( T& ),
								DirectX::XMFLOAT2& (*)( T& ), DirectX::XMFLOAT3& (*)( T& ), DirectX::XMFLOAT4& (*)( T& ),
								std::string& (*)( T& ), SettingRange& (*)( T& ),
								float T::*, bool T::*, int32_t T::*, uint32_t T::*, DirectX::XMFLOAT2 T::*, DirectX::XMFLOAT3 T::*,
								DirectX::XMFLOAT4 T::*, std::string T::*, SettingRange T::*>;

	// Колонка базы; у вектора и полосы — колонки компонент через запятую («rotation_x,rotation_y,rotation_z»,
	// «slope_from,slope_to») или одна текстом «x,y,z»; NULL в базе — значение по умолчанию структуры, пустая строка — NULL;
	// пусто — поле не хранится
	std::string column;
	const char* name = nullptr;		// свойство окна (без «/» — разделителя пути set); nullptr — только в базе
	Access access;
	float low = 0.0f;				// границы контрола; low < high — с ними (верхняя в окне — не меньше значения)
	float high = 0.0f;
	const char* unit = "";
	std::string tooltip;
	uint32_t flags = 0;				// SettingFlag
	// Часть окна: подокно (правила слоёв по группам) или то, что объект добавляет по условию (импостер слоя расстановки);
	// "" — основная
	std::string group;
	// Целое с выбором из значений (размер кадра импостера): в окне — список, свойство — номер значения
	std::vector<int32_t> choices = {};
	// Подписи значений списка; пусто — сами значения («64», «128»)
	std::vector<std::string> labels = {};
};

template<typename T>
using SettingsSchema = std::vector<SettingField<T>>;

// Ссылка на член по доступу поля
template<typename T, typename V>
V& settingRef( V& ( *access )( T& ), T& values )
{
	return access( values );
}

template<typename T, typename V>
V& settingRef( V T::* access, T& values )
{
	return values.*access;
}

// Чтение без копии структуры (у воды в ней кривые и источники): доступ только возвращает ссылку на член
template<typename T, typename V>
const V& settingRef( V& ( *access )( T& ), const T& values )
{
	return access( const_cast<T&>( values ) );
}

template<typename T, typename V>
const V& settingRef( V T::* access, const T& values )
{
	return values.*access;
}

// Поле по колонке базы; nullptr — нет
template<typename T>
const SettingField<T>* findSettingField( const SettingsSchema<T>& schema, const char* column )
{
	for( const SettingField<T>& field : schema )
		if( field.column == column )
			return &field;
	return nullptr;
}

// Значения полей по порядку описания: f( const V& ) для каждого типа поля (отпечаток настроек — Fingerprint и т. п.); group —
// только поля этой части
template<typename T, typename F>
void forEachSettingValue( const SettingsSchema<T>& schema, const T& values, F f, const char* group = nullptr )
{
	for( const SettingField<T>& field : schema )
		if( !group || field.group == group )
			std::visit( [&]( auto access ) { f( settingRef( access, values ) ); }, field.access );
}

// Совпадает ли значение поля
template<typename T>
bool settingFieldEqual( const SettingField<T>& field, const T& a, const T& b )
{
	return std::visit( [&]( auto access )
	{
		const auto& l = settingRef( access, a );
		const auto& r = settingRef( access, b );
		using Value = std::remove_cv_t<std::remove_reference_t<decltype( l )>>;
		if constexpr( std::is_same_v<Value, DirectX::XMFLOAT2> )
			return l.x == r.x && l.y == r.y;
		else if constexpr( std::is_same_v<Value, DirectX::XMFLOAT3> )
			return l.x == r.x && l.y == r.y && l.z == r.z;
		else if constexpr( std::is_same_v<Value, DirectX::XMFLOAT4> )
			return l.x == r.x && l.y == r.y && l.z == r.z && l.w == r.w;
		else if constexpr( std::is_same_v<Value, SettingRange> )
			return l.from == r.from && l.to == r.to;
		else
			return l == r;
	}, field.access );
}

// Совпадают ли значения всех полей описания
template<typename T>
bool settingsEqual( const SettingsSchema<T>& schema, const T& a, const T& b )
{
	for( const SettingField<T>& field : schema )
		if( !settingFieldEqual( field, a, b ) )
			return false;
	return true;
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
