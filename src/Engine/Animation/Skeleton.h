#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <DirectXMath.h>

namespace GS
{

// Место кости относительно родителя — перемещение, поворот кватернионом, масштаб (как FTransform в UE); в файлах скелета и
// клипов — 10 float подряд
struct JointTransform
{
	DirectX::XMFLOAT3 translation = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
	DirectX::XMFLOAT4 rotation = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f );
	DirectX::XMFLOAT3 scale = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );
};
static_assert( sizeof( JointTransform ) == 40, "JointTransform: 10 float, как в файлах .skel и .anim" );

// Скелет модели — как Skeleton в UE и skin в glTF: кости с родителями (родитель — раньше потомка) и поза покоя. Меш модели
// лежит в позе покоя (Tools/import_gltf.py переносит в вершины общую матрицу привязки), поэтому обратные матрицы привязки —
// обратные к местам костей в позе покоя в пространстве модели. Номер кости — индекс в весах вершин (блок SKIN файла меша)
class Skeleton
{
public:
	static constexpr size_t maxJoints = 256;	// индекс кости в весах вершины — uint8

	struct Joint
	{
		std::string name;
		int32_t parent = -1;	// −1 — корень
		JointTransform rest;
	};

	// Кости — родитель раньше потомка; false — reason
	bool create( std::vector<Joint> joints, std::string& reason );
	// Файл .skel (Tools/import_gltf.py): метка DMSK, версия, число костей; у кости — длина имени, имя (UTF-8), родитель,
	// поза покоя (10 float)
	bool parse( const uint8_t* data, size_t size, std::string& reason );
	bool load( const std::string& path, std::string& reason );

	size_t jointCount() const { return m_joints.size(); }
	const Joint& joint( size_t index ) const { return m_joints[index]; }
	const std::vector<Joint>& joints() const { return m_joints; }
	// Номер кости по имени, −1 — нет
	int32_t find( const std::string& name ) const;
	// Обратные матрицы привязки (DirectXMath: точка-строка × матрица)
	const std::vector<DirectX::XMFLOAT4X4>& inverseBind() const { return m_inverseBind; }

private:
	std::vector<Joint> m_joints;
	std::vector<DirectX::XMFLOAT4X4> m_inverseBind;
};

// Чтение двоичных файлов анимации (.skel, .anim) — по порядку, little-endian; вышли за конец — ok() false
class BinaryReader
{
public:
	BinaryReader( const uint8_t* data, size_t size ) : m_data( data ), m_size( size ) {}

	template<typename T> T read()
	{
		T value{};
		bytes( &value, sizeof( T ) );
		return value;
	}
	void bytes( void* out, size_t count );
	bool ok() const { return m_ok; }
	size_t remaining() const { return m_ok ? m_size - m_offset : 0; }

private:
	const uint8_t* m_data;
	size_t m_size;
	size_t m_offset = 0;
	bool m_ok = true;
};

// Файл целиком; false — reason
bool readFileBytes( const std::string& path, std::vector<uint8_t>& data, std::string& reason );

}
