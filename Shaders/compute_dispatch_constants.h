////////////////////////////////////////////////////////////////////////////////
// Константы сетки старых вызовов DMComputeShader::Dispatch (b2): шаг групп, размер области, время. Общие для C++ и HLSL
// — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_COMPUTE_DISPATCH_CONSTANTS_H
#define DM_COMPUTE_DISPATCH_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

// DMComputeShader::Dispatch: b_rect — размер сетки в ячейках
#define DM_FIELD_PREFIX b_
DM_CONSTANTS( ThreadsData, SLOT_CB_PASS )
{
	DM_FIELD( float, groupDim );
	DM_FIELD( float2, rect );
	DM_FIELD( float, elapsedTime );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
