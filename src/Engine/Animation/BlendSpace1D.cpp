#include "BlendSpace1D.h"
#include <algorithm>

namespace GS
{

void BlendSpace1D::addSample( uint32_t clip, float speed, float duration )
{
	const Sample sample{ clip, std::max( speed, 0.0f ), duration };
	auto at = std::upper_bound( m_samples.begin(), m_samples.end(), sample.speed,
								[]( float value, const Sample& s ) { return value < s.speed; } );
	m_samples.insert( at, sample );
}

BlendSpace1D::Result BlendSpace1D::evaluate( float speed ) const
{
	Result result;
	if( m_samples.empty() )
		return result;
	auto weighted = []( const Sample& sample, float weight )
	{
		return Weighted{ sample.clip, weight, sample.duration, sample.speed > 0.0f };
	};
	// Длина шага клипа на ходу за цикл, м
	auto stride = []( const Sample& sample ) { return sample.speed * sample.duration; };

	speed = std::max( speed, 0.0f );
	size_t upper = 0;
	while( upper < m_samples.size() && m_samples[upper].speed < speed )
		++upper;
	if( upper == 0 || upper == m_samples.size() )
	{
		// Ниже первой точки или выше последней — одна точка, на ходу — быстрее или медленнее
		const Sample& sample = m_samples[upper == 0 ? 0 : upper - 1];
		result.samples[0] = weighted( sample, 1.0f );
		result.count = 1;
		if( stride( sample ) > 0.0f )
			result.phaseRate = speed / stride( sample );
		return result;
	}
	const Sample& a = m_samples[upper - 1];
	const Sample& b = m_samples[upper];
	const float weight = ( speed - a.speed ) / std::max( b.speed - a.speed, 1e-6f );
	result.samples[0] = weighted( a, 1.0f - weight );
	result.samples[1] = weighted( b, weight );
	result.count = 2;
	// Смешанный шаг — только клипы на ходу: у покоя ноги стоят
	float strideSum = 0.0f;
	if( a.speed > 0.0f )
		strideSum += ( 1.0f - weight ) * stride( a );
	strideSum += weight * stride( b );
	result.phaseRate = strideSum > 0.0f ? speed / strideSum : 0.0f;
	return result;
}

}
