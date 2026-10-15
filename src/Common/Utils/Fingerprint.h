#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// Отпечаток FNV-1a 64 — ключ кэшей (эрозия, генерация кривых русел, течение воды, текстуры, кадры импостеров, DXIL,
// пайплайны): те же данные — тот же ключ. Хэш FNV в движке один — этот
struct Fingerprint
{
	// Начало по умолчанию — не стандартное: у стандартного offset basis (fnvOffsetBasis) в константе потерялась последняя
	// цифра. С ним посчитаны ключи кэшей на диске — не менять: сменились бы все, а с ключом генерации — и сгенерированные
	// кривые русел в базе (StreamGeneration)
	static constexpr uint64_t defaultBasis = 1469598103934665603ull;
	// Стандартный offset basis FNV-1a — у ключей, которые посчитаны с ним: кэш DXIL, имена пайплайнов в библиотеке
	static constexpr uint64_t fnvOffsetBasis = 14695981039346656037ull;
	static constexpr uint64_t prime = 1099511628211ull;

	uint64_t value = defaultBasis;

	Fingerprint() = default;
	// С другого начала: стандартный offset basis или прежний отпечаток (ключ шейдера — от отпечатка каталога)
	explicit Fingerprint( uint64_t basis ) : value( basis ) {}

	void add( const void* data, size_t size )
	{
		const uint8_t* bytes = static_cast<const uint8_t*>( data );
		for( size_t i = 0; i < size; ++i )
			value = ( value ^ bytes[i] ) * prime;
	}
	// Значение простого типа — его байты (у строки в объекте указатель — для неё своя перегрузка)
	template<typename T> void add( const T& v ) { add( &v, sizeof( v ) ); }
	// Текст — его символы
	void add( const std::string& text ) { add( text.data(), text.size() ); }
	// Большие массивы (десятки МБ) — по 8 байт за шаг с перемешиванием старших битов: в разы быстрее add, но ключ
	// у тех же байт другой
	void addWords( const void* data, size_t size )
	{
		const uint8_t* bytes = static_cast<const uint8_t*>( data );
		const size_t words = size / sizeof( uint64_t );
		for( size_t i = 0; i < words; ++i )
		{
			uint64_t word;
			std::memcpy( &word, bytes + i * sizeof( uint64_t ), sizeof( word ) );
			value = ( value ^ word ) * prime;
			value ^= value >> 32;
		}
		add( bytes + words * sizeof( uint64_t ), size - words * sizeof( uint64_t ) );
	}
};
