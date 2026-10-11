#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Отпечаток FNV-1a 64 — ключ кэшей на диске (эрозия, генерация кривых русел, течение воды): те же данные — тот же ключ
struct Fingerprint
{
	uint64_t value = 1469598103934665603ull;
	void add( const void* data, size_t size )
	{
		const uint8_t* bytes = static_cast<const uint8_t*>( data );
		for( size_t i = 0; i < size; ++i )
			value = ( value ^ bytes[i] ) * 1099511628211ull;
	}
	template<typename T> void add( const T& v ) { add( &v, sizeof( v ) ); }
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
			value = ( value ^ word ) * 1099511628211ull;
			value ^= value >> 32;
		}
		add( bytes + words * sizeof( uint64_t ), size - words * sizeof( uint64_t ) );
	}
};
