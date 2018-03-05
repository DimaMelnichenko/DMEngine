#pragma once

enum AbilityType
{
	None = 0,
	TransformBuffer = 1
};

class DMAbility
{
public:
	DMAbility();
	virtual ~DMAbility();

	virtual AbilityType type() = 0;
};

