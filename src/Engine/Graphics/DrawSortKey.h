#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace GS
{

// Ключи сортировки команд прохода (Renderer::buildCommands, buildShadowCommands) — чистые функции чисел, без мешей и
// устройства (их проверяют тесты, tests/test_render_logic.cpp). Меньший ключ рисуется раньше

// Непрозрачная команда: объекты в порядке сцены (8 бит), внутри — материал (16), его фаза — вариант шейдера (8: у PBR с
// экземплярами расстановки и запеканием их ~26), растеризатор (4); группа (меш с параметрами, 28 бит) последней —
// одинаковые меши встают подряд для инстансного вызова
inline uint64_t opaqueSortKey( uint32_t ownerOrder, uint32_t materialId, int phase, uint32_t raster, uint32_t instanceGroup )
{
	return ( static_cast<uint64_t>( ownerOrder & 0xFF ) << 56 ) | ( static_cast<uint64_t>( materialId & 0xFFFF ) << 40 ) |
		   ( ( static_cast<uint64_t>( phase ) & 0xFF ) << 32 ) | ( static_cast<uint64_t>( raster & 0xF ) << 28 ) |
		   ( instanceGroup & 0x0FFFFFFF );
}

// Прозрачная команда: дальние раньше, при равном расстоянии — порядок объектов сцены и номер
inline uint64_t transparentSortKey( float distance, uint32_t ownerOrder, uint32_t sequence )
{
	uint32_t bits;
	const float positive = std::max( distance, 0.0f );
	std::memcpy( &bits, &positive, sizeof( bits ) );	// у неотрицательных float порядок битов совпадает с порядком чисел
	return ( static_cast<uint64_t>( 0xFFFFFFFFu - bits ) << 32 ) | ( static_cast<uint64_t>( ownerOrder & 0xFF ) << 24 ) |
		   ( sequence & 0xFFFFFF );
}

// Команда каскада теней: объект, материал, фаза «только глубина» (4 бита), группа
inline uint64_t shadowSortKey( uint32_t ownerOrder, uint32_t materialId, int phase, uint32_t instanceGroup )
{
	return ( static_cast<uint64_t>( ownerOrder & 0xFF ) << 56 ) | ( static_cast<uint64_t>( materialId & 0xFFFF ) << 40 ) |
		   ( static_cast<uint64_t>( phase & 0xF ) << 36 ) | instanceGroup;
}

// Свой вызов объекта (CustomBatch) в непрозрачном проходе и в тенях: только порядок объекта
inline uint64_t customSortKey( uint32_t ownerOrder )
{
	return static_cast<uint64_t>( ownerOrder & 0xFF ) << 56;
}

}
