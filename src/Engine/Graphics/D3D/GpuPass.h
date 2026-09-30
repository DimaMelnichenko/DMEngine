#pragma once

#include <cstdint>
#include <vector>
#include "GpuResources.h"

// Объявление прохода (шаг A5 плана переезда на D3D12; ≈ FrameGraph): имя, цели цвета и глубины с областью вывода,
// что проход читает и что пишет (UAV). DMD3D::beginPass ставит цели и снимает со входов всех стадий транзитные слоты
// (t0…SLOT_TRANSIENT_COUNT − 1) и виды тех ресурсов, в которые проход пишет; ресурсы прохода привязываются после него.
// В D3D12 из этих же объявлений ставятся барьеры переходов. Список проходов кадра с ресурсами — команда passes
struct PassDesc
{
	struct Target
	{
		const TargetView* view = nullptr;
		const char* name = "";
	};
	struct Read
	{
		const ShaderView* view = nullptr;
		const char* name = "";
	};
	struct Write
	{
		const StorageView* view = nullptr;
		const char* name = "";
	};

	const char* name = "";
	std::vector<Target> colors;		// цели цвета (обычно одна); пусто и без глубины — compute-проход
	Target depth;					// цель глубины или пусто
	uint32_t width = 0;				// область вывода; 0 — не меняется (compute)
	uint32_t height = 0;
	std::vector<Read> reads;		// что проход читает — для списка проходов и барьеров D3D12
	std::vector<Write> writes;		// что пишет из compute
};
