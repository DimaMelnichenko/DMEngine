#include "ParameterType.h"
#include <unordered_map>

ParameterType convertToParamType( uint32_t val )
{
	switch( val )
	{
		case 1:
			return ParameterType::float4;
		case 2:
			return ParameterType::textureId;
		case 3:
			return ParameterType::scalarValue;
		case 4:
			return ParameterType::none;
	}

	return ParameterType::none;
}

namespace
{
	
}

const std::string& convertParamTypeToString( ParameterType val )
{
	static bool init = false;
	static std::unordered_map<ParameterType, std::string> paramTypeNames;
	if( !init )
	{
		init = true;
		paramTypeNames[ParameterType::float4] = "float4";
		paramTypeNames[ParameterType::textureId] = "texureId";
		paramTypeNames[ParameterType::scalarValue] = "scalarValue";
		paramTypeNames[ParameterType::none] = "none";
	}

	return paramTypeNames[val];
}

uint32_t convertParamTypeToInt( ParameterType val )
{
	switch( val )
	{
		case float4:
			return 1;
			break;
		case textureId:
			return 2;
			break;
		case scalarValue:
			return 3;
			break;
		case none:
			return 4;
			break;
	}

	return 4;
}