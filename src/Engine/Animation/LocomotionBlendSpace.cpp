#include "LocomotionBlendSpace.h"
#include <algorithm>
#include <cmath>

namespace GS
{

void LocomotionBlendSpace::addSample( uint32_t clip, float speed, float duration, float turn )
{
	const Sample sample{ clip, std::max( speed, 0.0f ), duration, turn };
	if( turn != 0.0f )
		m_turns.push_back( sample );
	else
	{
		Row row;
		row.straight = sample;
		auto at = std::upper_bound( m_rows.begin(), m_rows.end(), sample.speed,
									[]( float value, const Row& r ) { return value < r.straight.speed; } );
		m_rows.insert( at, row );
	}
	attachTurns();
}

void LocomotionBlendSpace::attachTurns()
{
	for( Row& row : m_rows )
		row.hasLeft = row.hasRight = false;
	for( const Sample& turn : m_turns )
	{
		// Ряд ближайшей скорости на ходу; к покою поворот не прикрепляется
		Row* nearest = nullptr;
		for( Row& row : m_rows )
			if( row.straight.speed > 0.0f &&
				( !nearest || std::abs( row.straight.speed - turn.speed ) < std::abs( nearest->straight.speed - turn.speed ) ) )
				nearest = &row;
		if( !nearest )
			continue;
		// В каждую сторону — первый вариант
		if( turn.turn < 0.0f && !nearest->hasLeft )
		{
			nearest->left = turn;
			nearest->hasLeft = true;
		}
		else if( turn.turn > 0.0f && !nearest->hasRight )
		{
			nearest->right = turn;
			nearest->hasRight = true;
		}
	}
}

LocomotionBlendSpace::Result LocomotionBlendSpace::evaluate( float speed, float turn ) const
{
	Result result;
	if( m_rows.empty() )
		return result;
	auto add = [&result]( const Sample& sample, float weight )
	{
		if( weight > 0.0f && result.count < maxWeighted )
			result.samples[result.count++] = Weighted{ sample.clip, weight, sample.duration, sample.speed > 0.0f };
	};
	// Длина шага клипа на ходу за цикл, м; смешанный шаг — только клипы на ходу: у покоя ноги стоят
	float strideSum = 0.0f;
	auto addRow = [&]( const Row& row, float weight )
	{
		const Sample* variant = turn < 0.0f && row.hasLeft ? &row.left : turn > 0.0f && row.hasRight ? &row.right : nullptr;
		const float share = variant ? std::min( std::abs( turn ) / std::abs( variant->turn ), 1.0f ) : 0.0f;
		add( row.straight, weight * ( 1.0f - share ) );
		strideSum += weight * ( 1.0f - share ) * row.straight.speed * row.straight.duration;
		if( variant )
		{
			add( *variant, weight * share );
			strideSum += weight * share * variant->speed * variant->duration;
		}
	};

	speed = std::max( speed, 0.0f );
	size_t upper = 0;
	while( upper < m_rows.size() && m_rows[upper].straight.speed < speed )
		++upper;
	if( upper == 0 || upper == m_rows.size() )
	{
		// Ниже первого ряда или выше последнего — один ряд, на ходу — быстрее или медленнее
		addRow( m_rows[upper == 0 ? 0 : upper - 1], 1.0f );
		result.phaseRate = strideSum > 0.0f ? speed / strideSum : 0.0f;
		return result;
	}
	const Row& a = m_rows[upper - 1];
	const Row& b = m_rows[upper];
	const float weight = ( speed - a.straight.speed ) / std::max( b.straight.speed - a.straight.speed, 1e-6f );
	addRow( a, 1.0f - weight );
	addRow( b, weight );
	result.phaseRate = strideSum > 0.0f ? speed / strideSum : 0.0f;
	return result;
}

}
