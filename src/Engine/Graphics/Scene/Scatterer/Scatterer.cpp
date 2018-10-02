#include "Scatterer.h"
#include "D3D\DMD3D.h"
#include "System.h"

namespace GS
{

Scatterer::Scatterer()
{

}

Scatterer::~Scatterer()
{

}

bool Scatterer::addMesh( DMModel::LodBlock* lodBlock )
{
	LodStruct& lodStruct = m_lods[m_lods.size()];
	lodStruct.lodBlock = lodBlock;
	if( !lodStruct.scatterPass.createBuffers() )
		return false;

	return true;
}

bool Scatterer::Initialize()
{
	if( !m_computeShader.Initialize( "Shaders\\grass.cs", "main" ) )
		return false;

	if( !m_initShader.Initialize( "Shaders\\grass.cs", "init" ) )
		return false;

	return true;
}

void Scatterer::prerender()
{
	for( uint16_t i = 0; i < m_lods.size(); ++i )
	{
		const DMModel::LodBlock* block = m_lods[i].lodBlock;
		AbstractMesh* mesh = System::meshes().get( block->mesh ).get();
		m_lods[i].scatterPass.setInstanceParameters( m_initShader, mesh->indexCount(), mesh->indexOffset(), mesh->vertexOffset() );
	}
	
	DMD3D::instance().setSRV( SRVType::cs, 0, System::textures().get( 25 )->srv() );
	DMD3D::instance().setSRV( SRVType::cs, 1, System::textures().get( 17 )->srv() );
	

	for( uint16_t i = 0; i < m_lods.size(); ++i )
	{		
		switch(i)
		{
			case 0:
				m_lods[i].scatterPass.populateParams().nearBorder = 0.0;
				m_lods[i].scatterPass.populateParams().farBorder = 20.0;
				m_lods[i].scatterPass.populateParams().sizeMultipler = 0.003;
				m_lods[i].scatterPass.populateParams().density = 1.0 / 8;
				m_lods[i].scatterPass.populateParams().nearFallow = 0.0;
				m_lods[i].scatterPass.populateParams().farFallow = 2.0;
				DMD3D::instance().setSRV( SRVType::cs, 2, System::textures().get( "VereskDensity" )->srv() );
				break;
			case 1:
				m_lods[i].scatterPass.populateParams().nearBorder = 10.0;
				m_lods[i].scatterPass.populateParams().farBorder = 100.0;
				m_lods[i].scatterPass.populateParams().sizeMultipler = 1.05;
				m_lods[i].scatterPass.populateParams().density = 1.0 / 1.0;
				m_lods[i].scatterPass.populateParams().nearFallow = 10.0;
				m_lods[i].scatterPass.populateParams().farFallow = 10.0;
				DMD3D::instance().setSRV( SRVType::cs, 2, System::textures().get( "VereskDensity" )->srv() );
				break;
			case 2:
				m_lods[i].scatterPass.populateParams().nearBorder = 0.0;
				m_lods[i].scatterPass.populateParams().farBorder = 40.0;
				m_lods[i].scatterPass.populateParams().sizeMultipler = 1.0;
				m_lods[i].scatterPass.populateParams().density = 2.0;
				m_lods[i].scatterPass.populateParams().nearFallow = 0.0;
				m_lods[i].scatterPass.populateParams().farFallow = 5.0;
				DMD3D::instance().setSRV( SRVType::cs, 2, System::textures().get( "RomashkaDensity" )->srv() );
				break;
		}


		m_lods[i].scatterPass.populate( m_computeShader );
	}
}

void Scatterer::Render( uint16_t lod, uint16_t instanceSlot )
{
	DMD3D::instance().setSRV( SRVType::vs, instanceSlot, m_lods[lod].scatterPass.structuredBuffer() );
}

ID3D11Buffer* Scatterer::indirectArgsBuffer( uint16_t lod )
{
	return m_lods[lod].scatterPass.args();
}

DMModel::LodBlock* Scatterer::lodBlock( uint16_t lod )
{
	return m_lods[lod].lodBlock;
}

uint16_t Scatterer::lodCount()
{
	return m_lods.size();
}

}