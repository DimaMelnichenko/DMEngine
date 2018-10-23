#include "MaterialParameterKind.h"
#include "DBConnector.h"


MaterialParameterKind::MaterialParameterKind()
{
}


MaterialParameterKind::~MaterialParameterKind()
{
}

bool MaterialParameterKind::load()
{
	try
	{
		SQLite::Statement query( dbConnect().db(), "SELECT id, name, type_id FROM MaterialParameterKind" );
		while( query.executeStep() )
		{
			ItemType itemType;
			itemType.id = query.getColumn( 0 );

			m_lastId = ( m_lastId < itemType.id ) ? itemType.id : m_lastId;

			itemType.name = query.getColumn( 1 ).getString();
			itemType.type = convertToParamType( query.getColumn( 2 ).getInt() );
			m_dictionary.insert( { itemType.id, std::move( itemType ) } );
		}
	}
	catch( std::exception& e )
	{	
		return false;
	}

	return true;
}

const MaterialParameterKind::ItemType& MaterialParameterKind::get( uint32_t id ) const
{
	return m_dictionary.at( id );
}

const MaterialParameterKind::ItemType* MaterialParameterKind::get( const std::string& name ) const
{
	for( auto& pair : m_dictionary )
	{
		if( pair.second.name == name )
			return &pair.second;
	}

	return nullptr;
}

uint32_t MaterialParameterKind::add( const std::string& name, ParameterType type )
{
	for( auto& pair : m_dictionary )
	{
		if( pair.second.name == name )
			return 0;
	}

	ItemType itemType;
	itemType.id = ++m_lastId;
	itemType.name = name;
	itemType.type = type;

	if( insertRow( itemType ) )
		m_dictionary.insert( { itemType.id, std::move( itemType ) } );
	else
		return 0;

	return m_lastId;
}

bool MaterialParameterKind::insertRow( const MaterialParameterKind::ItemType& item )
{
	try
	{
		SQLite::Statement query( dbConnect().db(), "INSERT MaterialParameterKind ( id, name, type_id ) VALUES( :id, :name, :type)" );
		query.bind( ":id", item.id );
		query.bind( ":name", item.name );
		query.bind( ":type", convertParamTypeToInt( item.type ) );
		query.exec();
	}
	catch( std::exception e )
	{
		return false;
	}

	return true;
}