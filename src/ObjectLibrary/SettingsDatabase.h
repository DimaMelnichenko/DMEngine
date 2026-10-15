#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <SQLiteCpp/SQLiteCpp.h>
#include "Properties\SettingsSchema.h"
#include "Logger\Logger.h"
#include "Utils\utilites.h"

// База по описанию полей настроек (SettingsSchema): чтение строки и запись UPDATE — без списка колонок и привязок
// вручную. Число — REAL (через %g: 0,3 остаётся 0,3, а не 0,30000001), флажок и целое — INTEGER, вектор и полоса — колонки
// компонент или текст «x,y,z», строка — TEXT (пустая — NULL)

// Число float для базы — короткой десятичной записью
inline double settingsDatabaseFloat( float value )
{
	char text[32];
	std::snprintf( text, sizeof( text ), "%g", value );
	return std::atof( text );
}

namespace SettingsDatabaseDetail
{

// Колонки поля: одна или компоненты через запятую
inline std::vector<std::string> columns( const std::string& column )
{
	std::vector<std::string> result;
	std::stringstream stream( column );
	std::string name;
	while( std::getline( stream, name, ',' ) )
		result.push_back( name );
	return result;
}

// Есть ли колонка в строке (база старше описания — нет: поле остаётся значением по умолчанию)
inline bool hasColumn( SQLite::Statement& row, const std::string& name )
{
	try
	{
		row.getColumnIndex( name.c_str() );
		return true;
	}
	catch( const SQLite::Exception& )
	{
		return false;
	}
}

// Компоненты вектора и полосы подряд: x, y (z, w) или from, to
inline float* components( DirectX::XMFLOAT2& value ) { return &value.x; }
inline float* components( DirectX::XMFLOAT3& value ) { return &value.x; }
inline float* components( DirectX::XMFLOAT4& value ) { return &value.x; }
inline float* components( SettingRange& value ) { return &value.from; }
inline const float* components( const DirectX::XMFLOAT2& value ) { return &value.x; }
inline const float* components( const DirectX::XMFLOAT3& value ) { return &value.x; }
inline const float* components( const DirectX::XMFLOAT4& value ) { return &value.x; }
inline const float* components( const SettingRange& value ) { return &value.from; }

template<typename V>
constexpr uint32_t componentCount()
{
	return sizeof( V ) / sizeof( float );
}

}

// Поля из строки запроса (SELECT * или со всеми колонками описания): NULL — у поля остаётся значение values; what — для
// лога («Volumetric cloud 1»)
template<typename T>
void readSettings( SQLite::Statement& row, const SettingsSchema<T>& schema, T& values, const std::string& what )
{
	using namespace SettingsDatabaseDetail;
	for( const SettingField<T>& field : schema )
	{
		if( field.column.empty() )
			continue;
		const std::vector<std::string> names = columns( field.column );
		bool present = true;
		for( const std::string& name : names )
			present = present && hasColumn( row, name );
		if( !present )
		{
			LOG( what + ": no column " + field.column + ", the default is used" );
			continue;
		}
		std::visit( [&]( auto access )
		{
			auto& target = settingRef( access, values );
			using Value = std::remove_reference_t<decltype( target )>;
			if constexpr( std::is_same_v<Value, float> )
			{
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( !column.isNull() )
					target = static_cast<float>( column.getDouble() );
			}
			else if constexpr( std::is_same_v<Value, bool> )
			{
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( !column.isNull() )
					target = column.getInt() != 0;
			}
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
			{
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( !column.isNull() )
					target = static_cast<Value>( column.getInt64() );
			}
			else if constexpr( std::is_same_v<Value, std::string> )
			{
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( !column.isNull() )
					target = column.getString();
			}
			else
			{
				// Вектор и полоса: колонки компонент или текст «x,y,z»
				constexpr uint32_t count = componentCount<Value>();
				float* parts = components( target );
				if( names.size() == count )
				{
					for( uint32_t i = 0; i < count; ++i )
					{
						const SQLite::Column column = row.getColumn( names[i].c_str() );
						if( !column.isNull() )
							parts[i] = static_cast<float>( column.getDouble() );
					}
					return;
				}
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( column.isNull() )
					return;
				// Текст — как у остальных векторов базы (strToVec*: через запятую, пробелы допустимы)
				const std::string text = column.getString();
				bool ok = false;
				if constexpr( count == 2 )
				{
					DirectX::XMFLOAT2 parsed;
					ok = strToVec2( text, parsed );
					if( ok )
						parts[0] = parsed.x, parts[1] = parsed.y;
				}
				else if constexpr( count == 3 )
				{
					DirectX::XMFLOAT3 parsed;
					ok = strToVec3( text, parsed );
					if( ok )
						parts[0] = parsed.x, parts[1] = parsed.y, parts[2] = parsed.z;
				}
				else
				{
					DirectX::XMFLOAT4 parsed;
					ok = strToVec4( text, parsed );
					if( ok )
						parts[0] = parsed.x, parts[1] = parsed.y, parts[2] = parsed.z, parts[3] = parsed.w;
				}
				if( !ok )
					LOG( what + ": wrong " + names[0] + " '" + text + "'" );
			}
		}, field.access );
	}
}

// Колонки описания по порядку (у вектора и полосы — каждая компонента)
template<typename T>
std::vector<std::string> settingsColumns( const SettingsSchema<T>& schema )
{
	std::vector<std::string> result;
	for( const SettingField<T>& field : schema )
		for( const std::string& name : SettingsDatabaseDetail::columns( field.column ) )
			result.push_back( name );
	return result;
}

// «col = :col, …» — присваивания UPDATE для полей описания
template<typename T>
std::string settingsAssignments( const SettingsSchema<T>& schema )
{
	std::string result;
	for( const std::string& name : settingsColumns( schema ) )
		result += ( result.empty() ? "" : ", " ) + name + " = :" + name;
	return result;
}

// Значения полей — в параметры :col запроса (settingsAssignments или INSERT со списком settingsColumns)
template<typename T>
void bindSettings( SQLite::Statement& statement, const SettingsSchema<T>& schema, const T& values )
{
	using namespace SettingsDatabaseDetail;
	for( const SettingField<T>& field : schema )
	{
		if( field.column.empty() )
			continue;
		const std::vector<std::string> names = columns( field.column );
		std::visit( [&]( auto access )
		{
			const auto& value = settingRef( access, values );
			using Value = std::remove_cv_t<std::remove_reference_t<decltype( value )>>;
			const std::string parameter = ":" + names[0];
			if constexpr( std::is_same_v<Value, float> )
				statement.bind( parameter, settingsDatabaseFloat( value ) );
			else if constexpr( std::is_same_v<Value, bool> )
				statement.bind( parameter, value ? 1 : 0 );
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
				statement.bind( parameter, static_cast<int64_t>( value ) );
			else if constexpr( std::is_same_v<Value, std::string> )
			{
				if( value.empty() )
					statement.bind( parameter );
				else
					statement.bind( parameter, value );
			}
			else
			{
				constexpr uint32_t count = componentCount<Value>();
				const float* parts = components( value );
				if( names.size() == count )
				{
					for( uint32_t i = 0; i < count; ++i )
						statement.bind( ":" + names[i], settingsDatabaseFloat( parts[i] ) );
					return;
				}
				std::string text;
				for( uint32_t i = 0; i < count; ++i )
				{
					char number[32];
					std::snprintf( number, sizeof( number ), "%g", parts[i] );
					text += ( i ? "," : "" ) + std::string( number );
				}
				statement.bind( parameter, text );
			}
		}, field.access );
	}
}

// UPDATE строки id таблицы table по описанию
template<typename T>
void updateSettings( SQLite::Database& db, const char* table, int64_t id, const SettingsSchema<T>& schema, const T& values )
{
	SQLite::Statement update( db, std::string( "UPDATE " ) + table + " SET " + settingsAssignments( schema ) + " WHERE id = :id" );
	bindSettings( update, schema, values );
	update.bind( ":id", id );
	update.exec();
}
