#include "EditHistory.h"
#include <cstring>

namespace Editor
{

namespace
{

// Значения равны: тот же тип и те же байты (у XMFLOAT* сравнения нет)
bool equal( const Property::Value& a, const Property::Value& b )
{
	if( a.index() != b.index() )
		return false;
	return std::visit( [&b]( const auto& value )
	{
		using Type = std::decay_t<decltype( value )>;
		return std::memcmp( &value, &std::get<Type>( b ), sizeof( Type ) ) == 0;
	}, a );
}

void collectInto( PropertyContainer& container, std::vector<Property*>& properties )
{
	for( const std::string& name : container.names() )
		properties.push_back( &container.property( name ) );
	for( PropertyContainer* sub : container.subContainer() )
		collectInto( *sub, properties );
}

const std::string noName;

}

void EditHistory::begin( const std::string& name, const std::vector<Property*>& properties, const std::vector<Property::Value>* before )
{
	if( m_editing )
		end();
	m_pending.name = name;
	m_pending.changes.clear();
	for( size_t i = 0; i < properties.size(); ++i )
	{
		const Property::Value& value = before && i < before->size() ? ( *before )[i] : properties[i]->value();
		m_pending.changes.push_back( { properties[i], value, value } );
	}
	m_editing = true;
}

void EditHistory::end()
{
	if( !m_editing )
		return;
	m_editing = false;
	Transaction transaction;
	transaction.name = std::move( m_pending.name );
	for( Change& change : m_pending.changes )
	{
		change.after = change.property->value();
		if( !equal( change.before, change.after ) )
			transaction.changes.push_back( change );
	}
	m_pending.changes.clear();
	if( transaction.changes.empty() )
		return;
	m_undo.push_back( std::move( transaction ) );
	if( m_undo.size() > maxTransactions )
		m_undo.erase( m_undo.begin() );
	m_redo.clear();
}

const std::string& EditHistory::undoName() const
{
	return m_undo.empty() ? noName : m_undo.back().name;
}

const std::string& EditHistory::redoName() const
{
	return m_redo.empty() ? noName : m_redo.back().name;
}

bool EditHistory::undo( std::string* name )
{
	if( !canUndo() )
		return false;
	Transaction transaction = std::move( m_undo.back() );
	m_undo.pop_back();
	for( const Change& change : transaction.changes )
		change.property->setValue( change.before );
	if( name )
		*name = transaction.name;
	m_redo.push_back( std::move( transaction ) );
	return true;
}

bool EditHistory::redo( std::string* name )
{
	if( !canRedo() )
		return false;
	Transaction transaction = std::move( m_redo.back() );
	m_redo.pop_back();
	for( const Change& change : transaction.changes )
		change.property->setValue( change.after );
	if( name )
		*name = transaction.name;
	m_undo.push_back( std::move( transaction ) );
	return true;
}

void EditHistory::clear()
{
	m_undo.clear();
	m_redo.clear();
	m_pending.changes.clear();
	m_editing = false;
}

std::vector<Property*> EditHistory::collect( PropertyContainer& container )
{
	std::vector<Property*> properties;
	collectInto( container, properties );
	return properties;
}

}
