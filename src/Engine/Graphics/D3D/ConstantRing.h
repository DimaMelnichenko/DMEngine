#pragma once

#include <d3d11_1.h>
#include <cstdint>
#include "Utils\utilites.h"
#include "DM3DUtils.h"

// Кольцо констант кадра (шаг A3 плана переезда на D3D12): один большой динамический константный буфер, участки по
// 256 байт — то же выравнивание, что у CBV D3D12. Запись — Map( WRITE_NO_OVERWRITE ) в свободный участок, первая
// запись кадра и переполнение — Map( WRITE_DISCARD ): драйвер подставляет новую память, и GPU дочитывает прошлый кадр
// из старой. Привязка — *SetConstantBuffers1 со смещением в единицах по 16 констант (D3D 11.1). В D3D12 это же кольцо
// живёт в upload-куче, участок привязывается root CBV
class ConstantRing
{
public:
	struct Stats
	{
		uint32_t frameBytes = 0;	// записано за кадр (с выравниванием)
		uint32_t frameWrites = 0;	// участков за кадр
		uint32_t frameWraps = 0;	// переполнений за кадр: кольцо мало, участок начат заново с DISCARD
		uint32_t capacity = 0;
	};

	bool initialize( ID3D11Device* device, ID3D11DeviceContext1* context, uint32_t bytes );
	// Начало кадра: статистика прошлого кадра сохраняется, следующая запись — с DISCARD
	void beginFrame();
	// Участок под size байт: указатель для записи до finishWrite(); offset и bytes — где он лежит (выравнены)
	void* beginWrite( uint32_t size, uint32_t& offset, uint32_t& bytes );
	void finishWrite();
	// Привязка участка стадии stage в слот slot
	void bind( SRVType stage, uint16_t slot, uint32_t offset, uint32_t bytes ) const;

	const Stats& lastFrameStats() const { return m_last; }

private:
	static constexpr uint32_t alignment = 256;

	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext1* m_context = nullptr;
	com_unique_ptr<ID3D11Buffer> m_buffer;
	uint32_t m_capacity = 0;
	uint32_t m_head = 0;
	bool m_discardNext = true;
	bool m_mapped = false;
	Stats m_current;
	Stats m_last;
};
