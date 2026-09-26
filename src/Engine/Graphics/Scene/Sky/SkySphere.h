#pragma once

#include "SceneObject.h"
#include "Common\DMTransform.h"

namespace GS
{

// Небесная сфера: модель неба уровня (колонка sky таблицы Levels), всегда вокруг камеры и за всей сценой
class SkySphere : public SceneObject
{
public:
	SkySphere();

	void setModel( uint32_t modelId );

	void update( const FrameContext& frame ) override;
	void render( const FrameContext& frame, RenderPass pass ) override;

private:
	uint32_t m_modelId = 0;	// 0 — неба нет: id моделей в base.db3 начинаются с 1
	DMTransform m_transform;	// центр — в камере
};

}
