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
// вручную. Число — REAL (через %g: 0,3 остаётся 0,3, а не 0,30000001), флажок и целое — INTEGER, вектор — колонки
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

// Колонки поля: одна или компоненты вектора через запятую
inline std::vector<std::string> columns( const char* column )
{
	std::vector<std::string> result;
	std::stringstream stream( column );
	std::string name;
	while( std::getline( stream, name, ',' ) )
		result.push_back( name );
	return result;
}

template<typename V>
float* components( V& value )
{
	return &value.x;
}

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
		if( !field.column )
			continue;
		const std::vector<std::string> names = columns( field.column );
		std::visit( [&]( auto access )
		{
			using Value = std::remove_reference_t<decltype( access( values ) )>;
			if constexpr( std::is_same_v<Value, float> )
			{
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( !column.isNull() )
					access( values ) = static_cast<float>( column.getDouble() );
			}
			else if constexpr( std::is_same_v<Value, bool> )
			{
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( !column.isNull() )
					access( values ) = column.getInt() != 0;
			}
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
			{
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( !column.isNull() )
					access( values ) = static_cast<Value>( column.getInt64() );
			}
			else if constexpr( std::is_same_v<Value, std::string> )
			{
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( !column.isNull() )
					access( values ) = column.getString();
			}
			else
			{
				// Вектор: колонки компонент или текст «x,y,z»
				constexpr uint32_t count = componentCount<Value>();
				float* target = components( access( values ) );
				if( names.size() == count )
				{
					for( uint32_t i = 0; i < count; ++i )
					{
						const SQLite::Column column = row.getColumn( names[i].c_str() );
						if( !column.isNull() )
							target[i] = static_cast<float>( column.getDouble() );
					}
					return;
				}
				const SQLite::Column column = row.getColumn( names[0].c_str() );
				if( column.isNull() )
					return;
				// Текст — как у остальных векторов базы (strToVec*: через запятую, пробелы допустимы)
				const std::string text = column.getString();
				Value parsed = access( values );
				bool ok = false;
				if constexpr( std::is_same_v<Value, DirectX::XMFLOAT2> )
					ok = strToVec2( text, parsed );
				else if constexpr( std::is_same_v<Value, DirectX::XMFLOAT3> )
					ok = strToVec3( text, parsed );
				else
					ok = strToVec4( text, parsed );
				if( ok )
					access( values ) = parsed;
				else
					LOG( what + ": wrong " + names[0] + " '" + text + "'" );
			}
		}, field.access );
	}
}

// «col = :col, …» — присваивания UPDATE для полей описания
template<typename T>
std::string settingsAssignments( const SettingsSchema<T>& schema )
{
	std::string result;
	for( const SettingField<T>& field : schema )
	{
		if( !field.column )
			continue;
		for( const std::string& name : SettingsDatabaseDetail::columns( field.column ) )
			result += ( result.empty() ? "" : ", " ) + name + " = :" + name;
	}
	return result;
}

// Значения полей — в параметры :col запроса из settingsAssignments
template<typename T>
void bindSettings( SQLite::Statement& statement, const SettingsSchema<T>& schema, const T& values )
{
	using namespace SettingsDatabaseDetail;
	T copy = values;
	for( const SettingField<T>& field : schema )
	{
		if( !field.column )
			continue;
		const std::vector<std::string> names = columns( field.column );
		std::visit( [&]( auto access )
		{
			using Value = std::remove_reference_t<decltype( access( copy ) )>;
			const std::string parameter = ":" + names[0];
			if constexpr( std::is_same_v<Value, float> )
				statement.bind( parameter, settingsDatabaseFloat( access( copy ) ) );
			else if constexpr( std::is_same_v<Value, bool> )
				statement.bind( parameter, access( copy ) ? 1 : 0 );
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
				statement.bind( parameter, static_cast<int64_t>( access( copy ) ) );
			else if constexpr( std::is_same_v<Value, std::string> )
			{
				if( access( copy ).empty() )
					statement.bind( parameter );
				else
					statement.bind( parameter, access( copy ) );
			}
			else
			{
				constexpr uint32_t count = componentCount<Value>();
				const float* source = components( access( copy ) );
				if( names.size() == count )
				{
					for( uint32_t i = 0; i < count; ++i )
						statement.bind( ":" + names[i], settingsDatabaseFloat( source[i] ) );
					return;
				}
				std::string text;
				for( uint32_t i = 0; i < count; ++i )
				{
					char number[32];
					std::snprintf( number, sizeof( number ), "%g", source[i] );
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
