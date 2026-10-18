#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Skeleton.h"

namespace GS
{

// Клип — как AnimSequence в UE: локальные места всех костей скелета кадрами постоянной частоты (Tools/import_gltf.py
// пересэмплирует каналы glTF). Движение корня (root motion) импорт выносит из клипа — клип на месте; оно — кривой по кадрам
// и средними скоростью и поворотом: по ним шаг по скорости (BlendSpace1D) подгоняет темп, чтобы копыта не скользили
class AnimationClip
{
public:
	// Движение корня от первого кадра в пространстве модели: сдвиг по x, z (м) и поворот вокруг вертикали (рад)
	struct RootMotion
	{
		float x = 0.0f;
		float z = 0.0f;
		float yaw = 0.0f;
	};

	// Кадры подряд: frameCount × jointCount мест костей; false — reason
	bool create( uint32_t jointCount, float frameRate, std::vector<JointTransform> frames, bool looping, std::string& reason );
	// Файл .anim (Tools/import_gltf.py): метка DMAN, версия, число костей и кадров, частота, флаги (бит 0 — цикл), средние
	// скорость и поворот корня, границы модели по всем кадрам, кадры, кривая движения корня
	bool parse( const uint8_t* data, size_t size, std::string& reason );
	bool load( const std::string& path, std::string& reason );

	// Поза в момент time, с: у зацикленного — по модулю длительности, у разового — до последнего кадра; кадры — линейно
	void sample( double time, std::vector<JointTransform>& pose ) const;
	// Движение корня с from до to, с (to ≥ from), как root motion delta в UE: сдвиг в осях корня в момент from (x — вбок к +X,
	// z — вперёд) и поворот вокруг вертикали. У зацикленного — через стыки цикла, у разового — в пределах клипа
	RootMotion rootMotionDelta( double from, double to ) const;
	// Кривая корня в момент time в пределах клипа — линейно между кадрами
	RootMotion rootMotionAt( double time ) const;

	uint32_t jointCount() const { return m_jointCount; }
	uint32_t frameCount() const { return m_frameCount; }
	float frameRate() const { return m_frameRate; }
	float duration() const { return m_frameCount > 1 ? ( m_frameCount - 1 ) / m_frameRate : 0.0f; }
	bool looping() const { return m_looping; }
	// Средняя скорость корня по пути (м/с) и поворот (рад/с, + — к +X от +Z)
	float rootSpeed() const { return m_rootSpeed; }
	float rootTurn() const { return m_rootTurn; }
	void setRootMotion( float speed, float turn ) { m_rootSpeed = speed; m_rootTurn = turn; }
	// Кривая движения корня по кадрам (frameCount точек) — у клипа из create; из файла она своя
	void setRootMotionCurve( std::vector<RootMotion> curve ) { if( curve.size() == m_frameCount ) m_rootMotion = std::move( curve ); }
	const std::vector<RootMotion>& rootMotion() const { return m_rootMotion; }
	// Границы модели по всем кадрам (пространство модели)
	const DirectX::XMFLOAT3& boundsMin() const { return m_boundsMin; }
	const DirectX::XMFLOAT3& boundsMax() const { return m_boundsMax; }

private:
	uint32_t m_jointCount = 0;
	uint32_t m_frameCount = 0;
	float m_frameRate = 30.0f;
	bool m_looping = false;
	float m_rootSpeed = 0.0f;
	float m_rootTurn = 0.0f;
	DirectX::XMFLOAT3 m_boundsMin = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
	DirectX::XMFLOAT3 m_boundsMax = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
	std::vector<JointTransform> m_frames;
	std::vector<RootMotion> m_rootMotion;
};

}
