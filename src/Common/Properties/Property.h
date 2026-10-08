#pragma once

#include "DirectX.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <cassert>
#include <variant>
#include "Logger\Logger.h"


//------------------------------
// BasicProperty

enum class GUIControlType : int
{
	SLIDER = 1,
	COLOR = 2,
	DRAG = 3,
	TEXTURE = 4,
	LABEL = 5	// только для чтения: значение считает объект (у числа — текст вместо ползунка)
};

enum class ValueType : int
{
	BOOL = 0,
	FLOAT = 1,
	VECTOR2 = 2,
	VECTOR3 = 3,
	VECTOR4 = 4,
	INT = 5,
	UINT = 6,
};

//ValueType convertStrinTypeToType

class Property
{
private:
	using Container = std::variant<bool, float, DirectX::XMFLOAT2, DirectX::XMFLOAT3, DirectX::XMFLOAT4, int32_t, uint32_t>;
public:
	Property();

	template<class TYPE>
	Property( const std::string& name, const TYPE& value ) : m_name(name), m_value(value)
	{

	}
    virtual ~Property() = default;

	ValueType valueType();

	template<class TYPE>
	const TYPE& data() const
	{
		return std::get<TYPE>( m_value );
	}

	template<class TYPE>
	TYPE* dataPtr()
	{
		return std::get_if<TYPE>( &m_value );
	}

	template<class TYPE>
	const TYPE* dataPtr() const
	{
		return std::get_if<TYPE>( &m_value );
	}

	template<class TYPE>
	void setData( const TYPE& value )
	{
		m_value = value;
	}
	float low() const;
	float high() const;
	GUIControlType controlType();

	void setLow( float );
	void setHigh( float );
	void setControlType( GUIControlType type );

	// Для панели Details (необязательно): подсказка, единицы («lx», «m», «1/m»), формат числа (printf; пусто — по
	// величине значения) и логарифмический ползунок — для величин в несколько порядков (плотность, яркость)
	Property* setTooltip( const std::string& tooltip ) { m_tooltip = tooltip; return this; }
	Property* setUnit( const std::string& unit ) { m_unit = unit; return this; }
	Property* setFormat( const std::string& format ) { m_format = format; return this; }
	Property* setLogarithmic( bool logarithmic = true ) { m_logarithmic = logarithmic; return this; }
	// Шаг перетаскивания (GUIControlType::DRAG) на пиксель; 0 — по величине значения и диапазону
	Property* setDragSpeed( float speed ) { m_dragSpeed = speed; return this; }
	float dragSpeed() const { return m_dragSpeed; }
	const std::string& tooltip() const { return m_tooltip; }
	const std::string& unit() const { return m_unit; }
	const std::string& format() const { return m_format; }
	bool logarithmic() const { return m_logarithmic; }

	// Сохранённое значение — при регистрации в GUI и после сохранения уровня: панель отмечает изменённые после него
	// и сбрасывает к нему
	void markSaved();
	bool modified() const;
	void resetToSaved();
private:
	std::string m_name;
	float m_lowBorder = 0.0;
	float m_highBorder = 1.0;
	// Без setControlType — ползунок в границах low…high; логическим свойствам (флажок) тип не нужен
	GUIControlType m_controlType = GUIControlType::SLIDER;
	Container m_value;	// тип значения — индекс варианта (valueType)
	Container m_saved;
	bool m_hasSaved = false;
	std::string m_tooltip;
	std::string m_unit;
	std::string m_format;
	bool m_logarithmic = false;
	float m_dragSpeed = 0.0f;
};

