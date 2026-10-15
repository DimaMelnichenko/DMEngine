#pragma once

#include <algorithm>
#include "Properties\SettingsSchema.h"
#include "Properties\PropertyContainer.h"

// Окно по описанию полей настроек (SettingsSchema): свойства — в порядке описания, структура — из свойств

// Свойства полей с именем: все или только части group ("" — основная); значения — из values
template<typename T>
void addSettingProperties( PropertyContainer& container, const SettingsSchema<T>& schema, const T& values, const char* group = nullptr )
{
	T copy = values;
	for( const SettingField<T>& field : schema )
	{
		if( !field.name || ( group && std::string( field.group ) != group ) )
			continue;
		Property* property = std::visit( [&]( auto access ) -> Property*
		{
			using Value = std::remove_reference_t<decltype( access( copy ) )>;
			if constexpr( std::is_same_v<Value, std::string> )
				return nullptr;		// строк в окне нет: у такого поля свой контрол у объекта
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
			{
				if( !field.choices.empty() )
				{
					// Номер ближайшего значения из списка
					const int32_t index = nearestSettingChoice( field.choices, static_cast<int32_t>( access( copy ) ) );
					std::vector<std::string> items;
					for( int32_t choice : field.choices )
						items.push_back( std::to_string( choice ) );
					return container.insert( field.name, index )->setItems( items );
				}
				return container.insert( field.name, access( copy ) );
			}
			else
				return container.insert( field.name, access( copy ) );
		}, field.access );
		if( !property )
			continue;
		if( field.low < field.high )
		{
			float high = field.high;
			if( const float* value = property->dataPtr<float>() )
				high = std::max( high, *value );
			property->setLow( field.low );
			property->setHigh( high );
		}
		if( *field.unit )
			property->setUnit( field.unit );
		if( *field.tooltip )
			property->setTooltip( field.tooltip );
		if( field.flags & settingLogarithmic )
			property->setLogarithmic();
		if( field.flags & settingColor )
			property->setControlType( GUIControlType::COLOR );
		if( field.flags & settingTransient )
			property->setTransient();
	}
}

// Поля из свойств окна — те, что в нём есть (часть по условию могла не добавиться: у поля остаётся значение values)
template<typename T>
void readSettingProperties( const PropertyContainer& container, const SettingsSchema<T>& schema, T& values )
{
	for( const SettingField<T>& field : schema )
	{
		if( !field.name || !container.exists( field.name ) )
			continue;
		const Property& property = container[field.name];
		std::visit( [&]( auto access )
		{
			using Value = std::remove_reference_t<decltype( access( values ) )>;
			if constexpr( std::is_same_v<Value, std::string> )
				return;
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
			{
				if( !field.choices.empty() )
				{
					const int32_t index = std::clamp( property.data<int32_t>(), 0, static_cast<int32_t>( field.choices.size() ) - 1 );
					access( values ) = static_cast<Value>( field.choices[index] );
				}
				else
					access( values ) = property.data<Value>();
			}
			else if constexpr( std::is_same_v<Value, float> )
			{
				float value = property.data<float>();
				if( ( field.flags & settingClamp ) && field.low < field.high )
					value = std::clamp( value, field.low, field.high );
				else if( field.flags & settingClampLow )
					value = std::max( value, field.low );
				access( values ) = value;
			}
			else
				access( values ) = property.data<Value>();
		}, field.access );
	}
}
