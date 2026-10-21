#include "ShallowWater.h"
#include <algorithm>
#include <cmath>

namespace GS
{

ShallowWater::ShallowWater( uint32_t width, uint32_t height, const Settings& settings ) :
	m_width( width ),
	m_height( height ),
	m_settings( settings )
{
	const size_t cells = static_cast<size_t>( width ) * height;
	const size_t facesX = static_cast<size_t>( width + 1 ) * height;
	const size_t facesZ = static_cast<size_t>( width ) * ( height + 1 );
	m_bed.assign( cells, 0.0f );
	m_depth.assign( cells, 0.0f );
	m_cellFixed.assign( cells, 0 );
	m_outflowScale.assign( cells, 1.0f );
	m_u.assign( facesX, 0.0f );
	m_uNext.assign( facesX, 0.0f );
	m_uFixed.assign( facesX, 0 );
	m_qx.assign( facesX, 0.0f );
	m_w.assign( facesZ, 0.0f );
	m_wNext.assign( facesZ, 0.0f );
	m_wFixed.assign( facesZ, 0 );
	m_qz.assign( facesZ, 0.0f );
}

float ShallowWater::stableTimeStep() const
{
	float fastest = 1e-3f;
	for( size_t i = 0; i < m_depth.size(); ++i )
		fastest = std::max( fastest, std::sqrt( m_settings.gravity * std::max( m_depth[i], 0.0f ) ) );
	float speed = 0.0f;
	for( float u : m_u )
		speed = std::max( speed, std::fabs( u ) );
	for( float w : m_w )
		speed = std::max( speed, std::fabs( w ) );
	return m_settings.maxCourant * m_settings.cell / ( fastest + speed );
}

uint32_t ShallowWater::step( float dt )
{
	if( dt <= 0.0f )
		return 0;
	const uint32_t steps = std::clamp( static_cast<uint32_t>( std::ceil( dt / stableTimeStep() ) ), 1u, m_settings.maxSubsteps );
	for( uint32_t i = 0; i < steps; ++i )
		substep( dt / steps );
	return steps;
}

double ShallowWater::volume() const
{
	double sum = 0.0;
	for( size_t i = 0; i < m_depth.size(); ++i )
		if( !m_cellFixed[i] )
			sum += m_depth[i];
	return sum * m_settings.cell * m_settings.cell;
}

void ShallowWater::substep( float dt )
{
	computeFluxes( dt );
	updateDepth( dt );
	updateVelocity( dt );
}

void ShallowWater::computeFluxes( float dt )
{
	const float scale = dt / m_settings.cell;
	// Сколько вода уходит из ячейки за шаг (сумма исходящих потоков) — не больше, чем в ней есть: исходящие потоки ячейки
	// масштабируются одной долей
	for( uint32_t z = 0; z < m_height; ++z )
		for( uint32_t x = 0; x < m_width; ++x )
		{
			const size_t index = cellIndex( x, z );
			const float h = m_depth[index];
			const float out = ( std::max( m_u[faceXIndex( x + 1, z )], 0.0f ) + std::max( -m_u[faceXIndex( x, z )], 0.0f ) +
								std::max( m_w[faceZIndex( x, z + 1 )], 0.0f ) + std::max( -m_w[faceZIndex( x, z )], 0.0f ) ) *
							  h * scale;
			m_outflowScale[index] = out > h && out > 0.0f ? h / out : 1.0f;
		}
	// Расход через грань, м²/с: глубина ячейки, откуда течёт, × скорость × доля; внешняя грань — стенка, если не закреплена
	// (закреплённая — расход из ячейки внутри или в неё)
	for( uint32_t z = 0; z < m_height; ++z )
		for( uint32_t x = 0; x <= m_width; ++x )
		{
			const size_t index = faceXIndex( x, z );
			const float u = m_u[index];
			if( x == 0 || x == m_width )
			{
				m_qx[index] = m_uFixed[index] ? u * m_depth[cellIndex( x == 0 ? 0 : m_width - 1, z )] : 0.0f;
				continue;
			}
			const size_t from = u > 0.0f ? cellIndex( x - 1, z ) : cellIndex( x, z );
			m_qx[index] = u * m_depth[from] * m_outflowScale[from];
		}
	for( uint32_t z = 0; z <= m_height; ++z )
		for( uint32_t x = 0; x < m_width; ++x )
		{
			const size_t index = faceZIndex( x, z );
			const float w = m_w[index];
			if( z == 0 || z == m_height )
			{
				m_qz[index] = m_wFixed[index] ? w * m_depth[cellIndex( x, z == 0 ? 0 : m_height - 1 )] : 0.0f;
				continue;
			}
			const size_t from = w > 0.0f ? cellIndex( x, z - 1 ) : cellIndex( x, z );
			m_qz[index] = w * m_depth[from] * m_outflowScale[from];
		}
}

void ShallowWater::updateDepth( float dt )
{
	const float scale = dt / m_settings.cell;
	for( uint32_t z = 0; z < m_height; ++z )
		for( uint32_t x = 0; x < m_width; ++x )
		{
			const size_t index = cellIndex( x, z );
			if( m_cellFixed[index] )
				continue;
			const float divergence = m_qx[faceXIndex( x + 1, z )] - m_qx[faceXIndex( x, z )] + m_qz[faceZIndex( x, z + 1 )] - m_qz[faceZIndex( x, z )];
			m_depth[index] = std::max( m_depth[index] - scale * divergence, 0.0f );
		}
}

void ShallowWater::updateVelocity( float dt )
{
	const float g = m_settings.gravity;
	const float cell = m_settings.cell;
	const float limit = std::min( m_settings.maxSpeed, 0.5f * cell / dt );
	const float friction = g * m_settings.manning * m_settings.manning;
	const float minDepth = m_settings.minDepth;

	// Перенос импульса в консервативной форме (Stelling, Duinmeijer 2003, «A staggered conservative scheme for every Froude
	// number in rapidly varied shallow water flows»): у грани — расход ячеек и углов вокруг неё (средний по их граням), скорость
	// со стороны, откуда он идёт; ∂u/∂t + [Δ(q̄ u*) − u Δq̄] / (h̄ Δx) — так сохраняется импульс, и прыжок встаёт на сопряжённых
	// глубинах (Беланже), а фронт по сухому дну бежит со своей скоростью. Полулагранжев перенос (Chentanez, Müller 2010)
	// импульс не сохранял: стоячий прыжок уползал на 0,2 м/с, фронт по сухому шёл в 2,7 раза медленнее Риттера
	auto faceX = [&]( int x, int z ) { return m_u[faceXIndex( std::clamp( x, 0, static_cast<int>( m_width ) ), std::clamp( z, 0, static_cast<int>( m_height ) - 1 ) )]; };
	auto faceZ = [&]( int x, int z ) { return m_w[faceZIndex( std::clamp( x, 0, static_cast<int>( m_width ) - 1 ), std::clamp( z, 0, static_cast<int>( m_height ) ) )]; };
	auto qX = [&]( int x, int z ) { return x < 0 || x > static_cast<int>( m_width ) || z < 0 || z >= static_cast<int>( m_height ) ? 0.0f : m_qx[faceXIndex( x, z )]; };
	auto qZ = [&]( int x, int z ) { return x < 0 || x >= static_cast<int>( m_width ) || z < 0 || z > static_cast<int>( m_height ) ? 0.0f : m_qz[faceZIndex( x, z )]; };
	auto finish = [&]( float velocity, float upstream )
	{
		if( upstream <= minDepth )
			return 0.0f;
		if( friction > 0.0f )
			velocity /= 1.0f + dt * friction * std::fabs( velocity ) / std::pow( upstream, 4.0f / 3.0f );
		return std::clamp( velocity, -limit, limit );
	};

	for( int z = 0; z < static_cast<int>( m_height ); ++z )
		for( int x = 0; x <= static_cast<int>( m_width ); ++x )
		{
			const size_t index = faceXIndex( x, z );
			if( m_uFixed[index] || x == 0 || x == static_cast<int>( m_width ) )
			{
				m_uNext[index] = m_u[index];
				continue;
			}
			const size_t a = cellIndex( x - 1, z );
			const size_t b = cellIndex( x, z );
			const float u = m_u[index];
			const float depth = 0.5f * ( m_depth[a] + m_depth[b] );
			float advection = 0.0f;
			if( depth > minDepth )
			{
				// Вдоль X: ячейки x − 1 и x
				const float qa = 0.5f * ( qX( x - 1, z ) + qX( x, z ) );
				const float qb = 0.5f * ( qX( x, z ) + qX( x + 1, z ) );
				const float ua = qa > 0.0f ? faceX( x - 1, z ) : u;
				const float ub = qb > 0.0f ? u : faceX( x + 1, z );
				advection += ( qb * ub - qa * ua - u * ( qb - qa ) ) / ( depth * cell );
				// Поперёк (по Z): углы снизу и сверху грани
				const float qd = 0.5f * ( qZ( x - 1, z ) + qZ( x, z ) );
				const float qu = 0.5f * ( qZ( x - 1, z + 1 ) + qZ( x, z + 1 ) );
				const float ud = qd > 0.0f ? faceX( x, z - 1 ) : u;
				const float uu = qu > 0.0f ? u : faceX( x, z + 1 );
				advection += ( qu * uu - qd * ud - u * ( qu - qd ) ) / ( depth * cell );
			}
			float velocity = u - dt * advection - g * dt * ( ( m_bed[b] + m_depth[b] ) - ( m_bed[a] + m_depth[a] ) ) / cell;
			m_uNext[index] = finish( velocity, velocity > 0.0f ? m_depth[a] : m_depth[b] );
		}
	for( int z = 0; z <= static_cast<int>( m_height ); ++z )
		for( int x = 0; x < static_cast<int>( m_width ); ++x )
		{
			const size_t index = faceZIndex( x, z );
			if( m_wFixed[index] || z == 0 || z == static_cast<int>( m_height ) )
			{
				m_wNext[index] = m_w[index];
				continue;
			}
			const size_t a = cellIndex( x, z - 1 );
			const size_t b = cellIndex( x, z );
			const float w = m_w[index];
			const float depth = 0.5f * ( m_depth[a] + m_depth[b] );
			float advection = 0.0f;
			if( depth > minDepth )
			{
				// Вдоль Z: ячейки z − 1 и z
				const float qa = 0.5f * ( qZ( x, z - 1 ) + qZ( x, z ) );
				const float qb = 0.5f * ( qZ( x, z ) + qZ( x, z + 1 ) );
				const float wa = qa > 0.0f ? faceZ( x, z - 1 ) : w;
				const float wb = qb > 0.0f ? w : faceZ( x, z + 1 );
				advection += ( qb * wb - qa * wa - w * ( qb - qa ) ) / ( depth * cell );
				// Поперёк (по X): углы слева и справа грани
				const float ql = 0.5f * ( qX( x, z - 1 ) + qX( x, z ) );
				const float qr = 0.5f * ( qX( x + 1, z - 1 ) + qX( x + 1, z ) );
				const float wl = ql > 0.0f ? faceZ( x - 1, z ) : w;
				const float wr = qr > 0.0f ? w : faceZ( x + 1, z );
				advection += ( qr * wr - ql * wl - w * ( qr - ql ) ) / ( depth * cell );
			}
			float velocity = w - dt * advection - g * dt * ( ( m_bed[b] + m_depth[b] ) - ( m_bed[a] + m_depth[a] ) ) / cell;
			m_wNext[index] = finish( velocity, velocity > 0.0f ? m_depth[a] : m_depth[b] );
		}
	m_u.swap( m_uNext );
	m_w.swap( m_wNext );
}

}
