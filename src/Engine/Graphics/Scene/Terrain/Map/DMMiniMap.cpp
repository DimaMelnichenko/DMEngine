#include "DMMiniMap.h"


DMMiniMap::DMMiniMap( DMD3D* parent ) : DMSceneObject( parent )
{
	m_MiniMapBitmap = 0;
	m_Border = 0;
	m_Point = 0;
}


DMMiniMap::~DMMiniMap()
{
	
}

bool DMMiniMap::Initialize( int screenWidth, int screenHeight, D3DXMATRIX viewMatrix, float terrainWidth,
							   float terrainHeight )
{
	bool result;

	// Initialize the location of the mini-map on the screen.
	m_mapLocationX = 150;
	m_mapLocationY = 75;

	// Set the size of the mini-map.
	m_mapSizeX = 150.0f;
	m_mapSizeY = 150.0f;

	// Store the base view matrix.
	m_viewMatrix = viewMatrix;

	// Store the terrain size.
	m_terrainWidth = terrainWidth;
	m_terrainHeight = terrainHeight;

	m_minimap_texture = std::unique_ptr<DMTexture>( new DMTexture( m_dmd3d ) );
	m_minimap_texture->Initialize( L"colorm01.dds" );

	m_border_texture = std::unique_ptr<DMTexture>( new DMTexture( m_dmd3d ) );
	m_border_texture->Initialize( L"border01.dds" );

	m_point_texture = std::unique_ptr<DMTexture>( new DMTexture( m_dmd3d ) );
	m_point_texture->Initialize( L"point01.dds" );

	// Create the mini-map bitmap object.
	m_MiniMapBitmap = std::unique_ptr<DMBitmap>( new DMBitmap( m_dmd3d ) );
	if( !m_MiniMapBitmap )
	{
		return false;
	}

	// Initialize the mini-map bitmap object.
	result = m_MiniMapBitmap->Initialize( screenWidth, screenHeight, 150, 150 );
	if( !result )
	{
		MessageBox( 0, L"Could not initialize the mini-map object.", L"Error", MB_OK );
		return false;
	}

	

	// Create the border bitmap object.
	m_Border = std::unique_ptr<DMBitmap>( new DMBitmap( m_dmd3d ) );
	if( !m_Border )
	{
		return false;
	}

	// Initialize the border bitmap object.
	result = m_Border->Initialize( screenWidth, screenHeight, 154, 154 );
	if( !result )
	{
		MessageBox( 0, L"Could not initialize the border object.", L"Error", MB_OK );
		return false;
	}

	// Create the point bitmap object.
	m_Point = std::unique_ptr<DMBitmap>( new DMBitmap( m_dmd3d ) );
	if( !m_Point )
	{
		return false;
	}

	// Initialize the point bitmap object.
	result = m_Point->Initialize( screenWidth, screenHeight, 3, 3 );
	if( !result )
	{
		MessageBox( 0, L"Could not initialize the point object.", L"Error", MB_OK );
		return false;
	}

	return true;
}

bool DMMiniMap::Render( DMTextureShader* textureShader, D3DXMATRIX* worldMatrix, DMCamera* camera )
{
	bool result;

	// Put the border bitmap vertex and index buffers on the graphics pipeline to prepare them for drawing.
	result = m_Border->Render( ( m_mapLocationX - 2 ), ( m_mapLocationY - 2 ) );
	if( !result )
	{
		return false;
	}

	textureShader->Prepare( camera, 0 );
	{
		// Render the border bitmap using the texture shader.
		textureShader->setTexure( m_border_texture->GetTexture() );
		textureShader->Render( m_Border->GetIndexCount(), worldMatrix );

		// Put the mini-map bitmap vertex and index buffers on the graphics pipeline to prepare them for drawing.
		result = m_MiniMapBitmap->Render( m_mapLocationX, m_mapLocationY );
		if( !result )
		{
			return false;
		}

		// Render the mini-map bitmap using the texture shader.
		textureShader->setTexure( m_minimap_texture->GetTexture() );
		textureShader->Render( m_MiniMapBitmap->GetIndexCount(), worldMatrix );

		// Put the point bitmap vertex and index buffers on the graphics pipeline to prepare them for drawing.
		result = m_Point->Render( m_pointLocationX, m_pointLocationY );
		if( !result )
		{
			return false;
		}

		// Render the point bitmap using the texture shader.
		textureShader->setTexure( m_point_texture->GetTexture() );
		textureShader->Render( m_Point->GetIndexCount(), worldMatrix );
	}

	return true;
}

void DMMiniMap::PositionUpdate( float positionX, float positionZ )
{
	float percentX, percentY;


	// Ensure the point does not leave the minimap borders even if the camera goes past the terrain borders.
	if( positionX < 0 )
	{
		positionX = 0;
	}

	if( positionZ < 0 )
	{
		positionZ = 0;
	}

	if( positionX > m_terrainWidth )
	{
		positionX = m_terrainWidth;
	}

	if( positionZ > m_terrainHeight )
	{
		positionZ = m_terrainHeight;
	}

	// Calculate the position of the camera on the minimap in terms of percentage.
	percentX = positionX / m_terrainWidth;
	percentY = 1.0f - ( positionZ / m_terrainHeight );

	// Determine the pixel location of the point on the mini-map.
	m_pointLocationX = m_mapLocationX + (int)( percentX * m_mapSizeX );
	m_pointLocationY = m_mapLocationY + (int)( percentY * m_mapSizeY );

	// Subtract one from the location to center the point on the mini-map according to the 3x3 point pixel image size.
	m_pointLocationX = m_pointLocationX - 1;
	m_pointLocationY = m_pointLocationY - 1;

	return;
}