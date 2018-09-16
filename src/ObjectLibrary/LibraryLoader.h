#pragma once
#include <memory>
#include "SQLiteCpp\SQLiteCpp.h"
#include "Materials\Parameter\Parameter.h"
#include "Shaders\DMShader.h"


class LibraryLoader
{
public:
	LibraryLoader();
	~LibraryLoader();
	
	bool init();
	bool loadTexture( uint32_t idTexture );
	bool loadMaterial( uint32_t idMaterial );
	bool loadMaterial( const std::string& materialName );
	bool loadMesh( uint32_t idMesh );
	bool loadModelWithLOD( uint32_t idModel );
	bool loadMaterialParams( uint32_t idLod, GS::ParamSet& paramSet );
	bool loadMaterialParamDef( uint32_t idMaterial, GS::ParamSet& paramSet );
	bool loadShader( uint32_t idMaterial, GS::DMShader* );


	void save();
private:
	SQLite::Database* m_db;
};

