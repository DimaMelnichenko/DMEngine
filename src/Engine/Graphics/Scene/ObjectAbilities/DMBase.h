#pragma once

#include <unordered_map>
#include <memory>
#include "DMAbility.h"

enum ObjectType
{
	StaticObject = 1,
	MoveableObject = 2,
	Light = 3
};

class DMBase
{
public:
	DMBase();
	virtual ~DMBase();

	virtual ObjectType type() = 0;
	virtual const std::string& name() = 0;
	virtual const DMAbility* ability( unsigned int id ) = 0;

private:
	std::unordered_map<unsigned int, std::unique_ptr<DMAbility>> m_abilies;
};
