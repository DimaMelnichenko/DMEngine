#pragma once
#include <stdint.h>
#include <string>

enum ParameterType
{
	float4 = 1, textureId = 2, scalarValue = 3, none = 4
};

ParameterType convertToParamType( uint32_t val );
uint32_t convertParamTypeToInt( ParameterType val );
const std::string& convertParamTypeToString( ParameterType val );