#pragma once

#include <cstdint>
#include "DirectX.h"
#include "Utils\utilites.h"

// Кольцо данных кадра: один буфер в upload-куче, отображённый на всё время. У каждого кадра в полёте своя часть
// буфера: пока GPU читает данные прошлого кадра, CPU пишет в другую часть. Константы — участки по 256 байт (выравнивание
// CBV D3D12), привязываются root CBV по GPU-адресу (DMD3D::setConstantBuffer); структурные данные (инстансы, патчи,
// свет) — участки по своему шагу с временным SRV на кадр (DMD3D::endWrite). Переполнение части кадра — участок
// начинается с её начала и считается (Stats::frameWraps): значит, кольцо мало (DMD3D::constantRingBytes)
class ConstantRing
{
public:
	struct Stats
	{
		uint32_t frameBytes = 0;	// записано за кадр (с выравниванием)
		uint32_t frameWrites = 0;	// участков за кадр
		uint32_t frameWraps = 0;	// переполнений части кадра
		uint32_t capacity = 0;		// байт на кадр
	};

	// bytes — на все кадры: часть кадра — bytes / frames
	bool initialize( ID3D12Device* device, uint32_t bytes, uint32_t frames );
	// Начало кадра frameIndex (0 … frames − 1): статистика прошлого кадра сохраняется, запись — с начала его части
	void beginFrame( uint32_t frameIndex );
	// Участок под size байт с выравниванием начала и размера alignment: указатель для записи до finishWrite(); offset и
	// bytes — где он лежит
	void* beginWrite( uint32_t size, uint32_t& offset, uint32_t& bytes, uint32_t alignment = constantAlignment );
	void finishWrite();
	// GPU-адрес участка по его смещению
	D3D12_GPU_VIRTUAL_ADDRESS address( uint32_t offset ) const { return m_gpuAddress + offset; }
	ID3D12Resource* handle() const { return m_buffer.get(); }

	const Stats& lastFrameStats() const { return m_last; }

	static constexpr uint32_t constantAlignment = 256;

private:

	com_unique_ptr<ID3D12Resource> m_buffer;
	uint8_t* m_mapped = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS m_gpuAddress = 0;
	uint32_t m_frameBytes = 0;	// часть одного кадра
	uint32_t m_frames = 0;
	uint32_t m_frameStart = 0;	// начало части текущего кадра
	uint32_t m_head = 0;
	bool m_writing = false;
	Stats m_current;
	Stats m_last;
};
