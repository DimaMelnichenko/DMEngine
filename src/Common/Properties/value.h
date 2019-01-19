#pragma once

#include <string>
#include <type_traits>
#include <typeinfo>
#include "ValueType.h"
#include "DirectX.h"
#include "Utils/utilites.h"


//------------------------------------
// basic_type

class basic_type
{
public:
    virtual ~basic_type() = default;

    virtual basic_type* clone() const = 0;

    virtual const std::type_info& valueType() const = 0;
    virtual bool typeMatched(const basic_type&) const = 0;

	virtual std::string typeName() const = 0;
    virtual void setValue(const basic_type&) = 0;

    virtual std::string valueToString() const = 0;
    virtual void valueFromString(const std::string&) = 0;

    virtual void verboseValue() const = 0;

    virtual bool equals(const basic_type&) const = 0;

    virtual bool is_composite() const 
	{ 
		return false; 
	}
    virtual bool has_member(const std::string&) const 
	{ 
		throw std::logic_error("Type is not composite"); 
	}
    virtual const basic_type& get_member(const std::string&) const 
	{ 
		throw std::logic_error("Type is not composite"); 
	}
    virtual basic_type& get_member(const std::string&) 
	{ 
		throw std::logic_error("Type is not composite"); 
	}
};


//------------------------------------
// type_base<...>

template<typename TYPE>
class type_base: public basic_type
{
 public:
    typedef TYPE data_type;

public:
    type_base() : m_value(TYPE())
    {
    }
    
    explicit type_base(const TYPE& value) : m_value(value)
    {
    }

    const std::type_info& valueType() const
    {
        return typeid(TYPE);
    }

    std::string typeName() const
    {
        static std::string typeName(typeid(TYPE).name());

        return typeName;
    }

    bool typeMatched(const basic_type& obj) const
    {
        return ( typeName() == obj.typeName());
    }

    operator const TYPE&() const
    {
        return value();
    }

    const TYPE& value() const
    {
        return m_value;
    }


    void setValue(const TYPE& value)
    {
        m_value = value;
    }
    
    void setValue(const basic_type& obj)
    {
        const type_base<TYPE>& matched_obj = dynamic_cast<const type_base<TYPE>&>(obj);

        setValue(matched_obj.value());
    }
 
    bool equals(const TYPE& value) const
    {
        return this->value() == value;
    }

    template<typename ARG_TYPE>
    bool equals(const type_base<ARG_TYPE>& obj) const
    {
        static_assert(std::is_convertible<TYPE, ARG_TYPE>::value, "Types are not convertible");

        return this->value() == obj.value();
    }


    bool equals(const basic_type& obj) const
    {
        bool state = false;

        const type_base<TYPE>* typed_obj = dynamic_cast<const type_base<TYPE>*>(&obj);
        if( typed_obj )
            state = equals(typed_obj->value());

        return state;
    }


 protected:
    TYPE& value()
    {
        return m_value;
    }

	TYPE* valuePtr()
	{
		return &m_value;
	}


 private:
    TYPE m_value;
};


//------------------------------------
// type<...>

template<typename TYPE>
class type : public type_base<TYPE>
{
public:
    type() = default;   

    explicit type(const TYPE& value) : type_base<TYPE>(value)
    {
    }    

    virtual ~type() = default;

    type<TYPE>* clone() const
    {
        return new type(this->value());
    }    


    template<typename ARG_TYPE>
    type<TYPE>& operator=(const ARG_TYPE& value)
    {
        this->setValue(value);

        return *this;
    }     


    template<typename ARG_TYPE>
    type<TYPE>& operator=(const type_base<ARG_TYPE>& obj)
    {
        this->setValue(obj);

        return *this;
    }     
	
    std::string valueToString() const
    {
		return toString<TYPE>( this->value() );
    }

    void verboseValue() const
    {
    	//LOG(logbook, toString(this->value()) );
    }

    void valueFromString(const std::string& s)
    {	
		if( auto&[value, success] = fromString<TYPE>( s ); success )
		{
			this->setValue( value );
		}
    }
};

