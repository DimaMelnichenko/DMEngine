#pragma once

#include "DMTransformBuffer.h"
#include "DMAbility.h"

class DMAbilityTransformation : public DMAbility, public DMTransformBuffer
{
public:
	DMAbilityTransformation();
	~DMAbilityTransformation();
};

