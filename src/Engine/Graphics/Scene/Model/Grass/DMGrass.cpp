#include "DMGrass.h"
#include "D3D\DMD3D.h"
#include "System.h"

namespace GS
{

DMGrass::DMGrass()
{

}

DMGrass::~DMGrass()
{

}

bool DMGrass::addMesh( DMModel::LodBlock* lodBlock )
{
	LodStruct& lodStruct = m_lods[m_lods.size()];
	lodStruct.lodBlock = lodBlock;
	if( !lodStruct.grassLod.createBuffers() )
		return false;

	return true;
}

bool DMGrass::Initialize()
{
	if( !m_computeShader.Initialize( "Shaders\\grass.cs", "main" ) )
		return false;

	if( !m_initShader.Initialize( "Shaders\\grass.cs", "init" ) )
		return false;

	return true;
}

void DMGrass::prerender()
{
	for( uint16_t i = 0; i < m_lods.size(); ++i )
	{
		const DMModel::LodBlock* block = m_lods[i].lodBlock;
		AbstractMesh* mesh = System::meshes().get( block->mesh ).get();
		m_lods[i].grassLod.setInstanceParameters( m_initShader, mesh->indexCount(), mesh->indexOffset(), mesh->vertexOffset() );
	}
	
	DMD3D::instance().setSRV( SRVType::cs, 0, System::textures().get( 7 )->srv() );
	DMD3D::instance().setSRV( SRVType::cs, 1, System::textures().get( 17 )->srv() );
	DMD3D::instance().setSRV( SRVType::cs, 2, System::textures().get( 13 )->srv() );

	for( uint16_t i = 0; i < m_lods.size(); ++i )
	{		
		switch(i)
		{
			case 0:
				m_lods[i].grassLod.populateParams().nearBorder = 0.0;
				m_lods[i].grassLod.populateParams().farBorder = 20.0;
				m_lods[i].grassLod.populateParams().sizeMultipler = 0.003;
				m_lods[i].grassLod.populateParams().density = 1.0 / 8;
				m_lods[i].grassLod.populateParams().nearFallow = 0.0;
				m_lods[i].grassLod.populateParams().farFallow = 2.0;
				break;
			case 1:
				m_lods[i].grassLod.populateParams().nearBorder = 10.0;
				m_lods[i].grassLod.populateParams().farBorder = 200.0;
				m_lods[i].grassLod.populateParams().sizeMultipler = 1.3;
				m_lods[i].grassLod.populateParams().density = 1.0 / 1.0;
				m_lods[i].grassLod.populateParams().nearFallow = 10.0;
				m_lods[i].grassLod.populateParams().farFallow = 10.0;
				break;
			case 2:
				m_lods[i].grassLod.populateParams().nearBorder = 0.0;
				m_lods[i].grassLod.populateParams().farBorder = 100.0;
				m_lods[i].grassLod.populateParams().sizeMultipler = 1.0;
				m_lods[i].grassLod.populateParams().density = 3.0;
				m_lods[i].grassLod.populateParams().nearFallow = 0.0;
				m_lods[i].grassLod.populateParams().farFallow = 5.0;
				break;
		}


		m_lods[i].grassLod.populate( m_computeShader );
	}
}

void DMGrass::Render( uint16_t lod, uint16_t instanceSlot )
{
	DMD3D::instance().setSRV( SRVType::vs, instanceSlot, m_lods[lod].grassLod.vertexBuffer() );
}

ID3D11Buffer* DMGrass::indirectArgsBuffer( uint16_t lod )
{
	return m_lods[lod].grassLod.args();
}

DMModel::LodBlock* DMGrass::lodBlock( uint16_t lod )
{
	return m_lods[lod].lodBlock;
}

uint16_t DMGrass::lodCount()
{
	return m_lods.size();
}

}