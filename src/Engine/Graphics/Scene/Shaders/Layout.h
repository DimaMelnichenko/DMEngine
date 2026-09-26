#pragma once
#include "Utils\utilites.h"
#include "DirectX.h"
#include <unordered_map>

class Layout
{
public:
	Layout();
	~Layout();

	bool initLayouts();

	struct LayoutItem
	{
		com_unique_ptr<ID3D11InputLayout> inputLayout;
		std::string inputSemantic;
	};

private:
	std::unordered_map<std::string, LayoutItem> m_layouts;
};
