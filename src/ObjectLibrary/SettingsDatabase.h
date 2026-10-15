#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>
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

// Значение колонки для записи: NULL, целое, число (REAL короткой записью — settingsDatabaseFloat), текст. Правка ли это,
// решается в этом виде — как значение ляжет в базу: шум ниже записи (пересчёт углов в вектор) правкой не считается
using DbValue = std::variant<std::nullptr_t, int64_t, double, std::string>;
using DbColumns = std::vector<std::pair<std::string, DbValue>>;

inline DbValue dbFloat( float value )
{
	return settingsDatabaseFloat( value );
}

inline DbValue dbInt( int64_t value )
{
	return value;
}

// Текст; пустой — NULL
inline DbValue dbText( const std::string& text )
{
	return text.empty() ? DbValue( nullptr ) : DbValue( text );
}

template<typename Key>
void bindDbValue( SQLite::Statement& statement, const Key& parameter, const DbValue& value )
{
	std::visit( [&]( const auto& v )
	{
		using V = std::decay_t<decltype( v )>;
		if constexpr( std::is_same_v<V, std::nullptr_t> )
			statement.bind( parameter );
		else if constexpr( std::is_same_v<V, int64_t> )
			statement.bind( parameter, static_cast<long long>( v ) );
		else
			statement.bind( parameter, v );
	}, value );
}

// Колонки и значения полей описания по порядку (у вектора и полосы — колонки компонент или текст «x,y,z»)
template<typename T>
void settingsColumnValues( const SettingsSchema<T>& schema, const T& values, DbColumns& out )
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
			if constexpr( std::is_same_v<Value, float> )
				out.emplace_back( names[0], dbFloat( value ) );
			else if constexpr( std::is_same_v<Value, bool> )
				out.emplace_back( names[0], dbInt( value ? 1 : 0 ) );
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
				out.emplace_back( names[0], dbInt( static_cast<int64_t>( value ) ) );
			else if constexpr( std::is_same_v<Value, std::string> )
				out.emplace_back( names[0], dbText( value ) );
			else
			{
				constexpr uint32_t count = componentCount<Value>();
				const float* parts = components( value );
				if( names.size() == count )
				{
					for( uint32_t i = 0; i < count; ++i )
						out.emplace_back( names[i], dbFloat( parts[i] ) );
					return;
				}
				std::string text;
				for( uint32_t i = 0; i < count; ++i )
				{
					char number[32];
					std::snprintf( number, sizeof( number ), "%g", parts[i] );
					text += ( i ? "," : "" ) + std::string( number );
				}
				out.emplace_back( names[0], DbValue( text ) );
			}
		}, field.access );
	}
}

// Значения полей — в параметры :col запроса (INSERT со списком settingsColumns)
template<typename T>
void bindSettings( SQLite::Statement& statement, const SettingsSchema<T>& schema, const T& values )
{
	DbColumns columns;
	settingsColumnValues( schema, values, columns );
	for( const auto& [name, value] : columns )
		bindDbValue( statement, ":" + name, value );
}

// Запись строки только изменёнными колонками — «Save level» пишет правки, а не окна целиком (LevelSaveData): колонка
// пишется, если её значение отличается от прежнего (загруженного или сохранённого прошлым «Save level») или прежнего
// нет (новая строка). Остальные колонки строки не трогаются: NULL (значение по умолчанию) остаётся NULL, запись скриптом
// остаётся
class RowUpdate
{
public:
	// Колонка: значение и прежнее (nullptr — прежнего нет, пишется)
	RowUpdate& set( const std::string& column, const DbValue& value, const DbValue* previous = nullptr )
	{
		if( !previous || *previous != value )
			m_columns.emplace_back( column, value );
		return *this;
	}
	// Колонка из объекта: value( объект ) против value( *previous )
	template<typename T, typename F>
	RowUpdate& set( const std::string& column, const T& object, const T* previous, F value )
	{
		const DbValue current = value( object );
		if( !previous )
			return set( column, current );
		const DbValue before = value( *previous );
		return set( column, current, &before );
	}
	// Поля описания против прежних значений (nullptr — все поля)
	template<typename T>
	RowUpdate& settings( const SettingsSchema<T>& schema, const T& values, const T* previous )
	{
		DbColumns current, before;
		settingsColumnValues( schema, values, current );
		if( previous )
			settingsColumnValues( schema, *previous, before );
		for( size_t i = 0; i < current.size(); ++i )
			set( current[i].first, current[i].second, previous ? &before[i].second : nullptr );
		return *this;
	}
	bool empty() const { return m_columns.empty(); }

	// UPDATE table SET <колонки> WHERE <ключ>; писать нечего — без запроса. Возврат — изменено строк (0 — строки нет)
	int update( SQLite::Database& db, const std::string& table, const DbColumns& key ) const
	{
		if( m_columns.empty() )
			return 0;
		std::string sql = "UPDATE " + table + " SET ";
		for( size_t i = 0; i < m_columns.size(); ++i )
			sql += ( i ? ", " : "" ) + m_columns[i].first + " = ?";
		sql += " WHERE ";
		for( size_t i = 0; i < key.size(); ++i )
			sql += ( i ? " AND " : "" ) + key[i].first + " = ?";
		SQLite::Statement statement( db, sql );
		int index = 1;
		for( const auto& column : m_columns )
			bindDbValue( statement, index++, column.second );
		for( const auto& column : key )
			bindDbValue( statement, index++, column.second );
		return statement.exec();
	}
	// Строка на ключ (правила слоёв террейна, русла воды): UPDATE, а строки нет — INSERT ключа и колонок
	void upsert( SQLite::Database& db, const std::string& table, const DbColumns& key ) const
	{
		if( m_columns.empty() || update( db, table, key ) > 0 )
			return;
		std::string names, values;
		DbColumns all = key;
		all.insert( all.end(), m_columns.begin(), m_columns.end() );
		for( size_t i = 0; i < all.size(); ++i )
		{
			names += ( i ? ", " : "" ) + all[i].first;
			values += i ? ", ?" : "?";
		}
		SQLite::Statement statement( db, "INSERT INTO " + table + " (" + names + ") VALUES (" + values + ")" );
		for( size_t i = 0; i < all.size(); ++i )
			bindDbValue( statement, static_cast<int>( i + 1 ), all[i].second );
		statement.exec();
	}

private:
	DbColumns m_columns;
};

// UPDATE строки id таблицы table по описанию: только поля, изменённые против previous (nullptr — все)
template<typename T>
void updateSettings( SQLite::Database& db, const char* table, int64_t id, const SettingsSchema<T>& schema, const T& values,
					 const T* previous )
{
	RowUpdate().settings( schema, values, previous ).update( db, table, { { "id", dbInt( id ) } } );
}

// Прежнее значение строки по id из снимка (LevelSaveData); id 0 — новая строка, прежнего нет
template<typename T, typename F>
const T* findPrevious( const std::vector<T>& previous, uint32_t id, F idOf )
{
	if( id == 0 )
		return nullptr;
	for( const T& item : previous )
		if( idOf( item ) == id )
			return &item;
	return nullptr;
}
