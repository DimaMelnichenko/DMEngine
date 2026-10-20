#pragma once

#include <algorithm>
#include "Properties\SettingsSchema.h"
#include "Properties\PropertyContainer.h"

// Окно по описанию полей настроек (SettingsSchema): свойства — в порядке описания, структура — из свойств

// Свойства полей с именем: все или только части group ("" — основная); значения — из values
template<typename T>
void addSettingProperties( PropertyContainer& container, const SettingsSchema<T>& schema, const T& values, const char* group = nullptr )
{
	for( const SettingField<T>& field : schema )
	{
		if( !field.name || ( group && field.group != group ) )
			continue;
		Property* property = std::visit( [&]( auto access ) -> Property*
		{
			const auto& value = settingRef( access, values );
			using Value = std::remove_cv_t<std::remove_reference_t<decltype( value )>>;
			if constexpr( std::is_same_v<Value, std::string> )
				return nullptr;		// строк в окне нет: у такого поля свой контрол у объекта
			else if constexpr( std::is_same_v<Value, SettingRange> )
				return container.insert( field.name, DirectX::XMFLOAT2( value.from, value.to ) );
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
			{
				if( !field.choices.empty() )
				{
					// Номер ближайшего значения из списка
					const int32_t index = nearestSettingChoice( field.choices, static_cast<int32_t>( value ) );
					std::vector<std::string> items = field.labels;
					if( items.empty() )
						for( int32_t choice : field.choices )
							items.push_back( std::to_string( choice ) );
					return container.insert( field.name, index )->setItems( items );
				}
				// Целые в окне — int32 (контрол числа один на оба типа)
				return container.insert( field.name, static_cast<int32_t>( value ) );
			}
			else
				return container.insert( field.name, value );
		}, field.access );
		if( !property )
			continue;
		if( field.low < field.high )
		{
			float high = field.high;
			if( const float* value = property->dataPtr<float>() )
				high = std::max( high, ( field.flags & settingWideHigh ) ? *value * 4.0f : *value );
			else if( const int32_t* integer = property->dataPtr<int32_t>(); integer && field.choices.empty() )
				high = std::max( high, static_cast<float>( *integer ) );
			property->setLow( field.low );
			property->setHigh( high );
		}
		if( *field.unit )
			property->setUnit( field.unit );
		if( !field.tooltip.empty() )
			property->setTooltip( field.tooltip );
		if( field.flags & settingLogarithmic )
			property->setLogarithmic();
		if( field.flags & settingColor )
			property->setControlType( GUIControlType::COLOR );
		if( field.flags & settingDrag )
			property->setControlType( GUIControlType::DRAG );
		if( field.flags & settingRangeControl )
			property->setControlType( GUIControlType::RANGE );
		if( field.flags & settingTransient )
			property->setTransient();
		if( field.category )
			property->setCategory( field.category );
	}
}

// Значения структуры — в свойства окна, которые в нём есть (обратное readSettingProperties: пресет окружения уровня,
// выбранный на лету); group — как у readSettingProperties. Значение меняется через setData — у свойства новый номер правки
template<typename T>
void writeSettingProperties( PropertyContainer& container, const SettingsSchema<T>& schema, const T& values, const char* group = nullptr )
{
	for( const SettingField<T>& field : schema )
	{
		if( !field.name || ( group && field.group != group ) || !container.exists( field.name ) )
			continue;
		Property& property = container[field.name];
		std::visit( [&]( auto access )
		{
			const auto& value = settingRef( access, values );
			using Value = std::remove_cv_t<std::remove_reference_t<decltype( value )>>;
			if constexpr( std::is_same_v<Value, std::string> )
				return;
			else if constexpr( std::is_same_v<Value, SettingRange> )
				property.setData( DirectX::XMFLOAT2( value.from, value.to ) );
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
				property.setData( field.choices.empty() ? static_cast<int32_t>( value )
														: nearestSettingChoice( field.choices, static_cast<int32_t>( value ) ) );
			else
				property.setData( value );
		}, field.access );
	}
}

// Поля из свойств окна — те, что в нём есть (часть по условию могла не добавиться: у поля остаётся значение values); group —
// только поля этой части (подокно правил слоёв: имена в разных подокнах могут совпадать)
template<typename T>
void readSettingProperties( const PropertyContainer& container, const SettingsSchema<T>& schema, T& values, const char* group = nullptr )
{
	for( const SettingField<T>& field : schema )
	{
		if( !field.name || ( group && field.group != group ) || !container.exists( field.name ) )
			continue;
		const Property& property = container[field.name];
		std::visit( [&]( auto access )
		{
			auto& target = settingRef( access, values );
			using Value = std::remove_reference_t<decltype( target )>;
			if constexpr( std::is_same_v<Value, std::string> )
				return;
			else if constexpr( std::is_same_v<Value, SettingRange> )
			{
				const DirectX::XMFLOAT2 range = property.data<DirectX::XMFLOAT2>();
				target = { range.x, range.y };
			}
			else if constexpr( std::is_same_v<Value, int32_t> || std::is_same_v<Value, uint32_t> )
			{
				if( !field.choices.empty() )
				{
					const int32_t index = std::clamp( property.data<int32_t>(), 0, static_cast<int32_t>( field.choices.size() ) - 1 );
					target = static_cast<Value>( field.choices[index] );
					return;
				}
				int32_t value = property.data<int32_t>();
				if( ( field.flags & settingClamp ) && field.low < field.high )
					value = std::clamp( value, static_cast<int32_t>( field.low ), static_cast<int32_t>( field.high ) );
				else if( field.flags & settingClampLow )
					value = std::max( value, static_cast<int32_t>( field.low ) );
				if constexpr( std::is_same_v<Value, uint32_t> )
					value = std::max( value, 0 );
				target = static_cast<Value>( value );
			}
			else if constexpr( std::is_same_v<Value, float> )
			{
				float value = property.data<float>();
				if( ( field.flags & settingClamp ) && field.low < field.high )
					value = std::clamp( value, field.low, field.high );
				else if( field.flags & settingClampLow )
					value = std::max( value, field.low );
				target = value;
			}
			else
				target = property.data<Value>();
		}, field.access );
	}
}
