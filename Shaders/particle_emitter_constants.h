////////////////////////////////////////////////////////////////////////////////
// Константы эмиттера частиц (ParticleSystem, Shaders/particles.sh). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_PARTICLE_EMITTER_CONSTANTS_H
#define DM_PARTICLE_EMITTER_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( ParticleEmitterBuffer, b4 )
{
	DM_FIELD( float3, origin );			// экземпляр, м мира
	DM_FIELD( uint, spawn );				// particleSpawn*
	DM_FIELD( float, radius );
	DM_FIELD( float, heightMin );
	DM_FIELD( float, heightMax );
	DM_FIELD( uint, capacity );			// частиц в пуле
	DM_FIELD( uint, spawnCount );		// попыток рождения в этом кадре
	DM_FIELD( uint, frame );				// номер шага симуляции — зерно случайных чисел
	DM_FIELD( uint, emitterIndex );
	DM_FIELD( float, timeStep );			// с
	DM_FIELD( float, lifetimeMin );
	DM_FIELD( float, lifetimeMax );
	DM_FIELD( float, sizeStart );
	DM_FIELD( float, sizeEnd );
	DM_FIELD( float3, color );
	DM_FIELD( float, alpha );
	DM_FIELD( float3, velocity );
	DM_FIELD( float, velocitySpread );
	DM_FIELD( float, gravity );
	DM_FIELD( float, drag );
	DM_FIELD( float, wind );
	DM_FIELD( float, curl );
	DM_FIELD( float, curlScale );
	DM_FIELD( float, waterFlow );
	DM_FIELD( float, waterSpeed );
	DM_FIELD( uint, collide );
	DM_FIELD( float, fadeIn );
	DM_FIELD( float, fadeOut );
	DM_FIELD( float, transmission );
	DM_FIELD( float, emissive );
	DM_FIELD( uint, shape );				// particleShape*
	DM_FIELD( uint, hasMask );
	DM_FIELD( uint, hasTerrain );
	DM_FIELD( float, viewportHeight );	// пикселей кадра по вертикали — наименьший размер частицы на экране
	DM_FIELD( float, sprayHeightMin );	// fall: брызги подлетают над водой на столько, м
	DM_FIELD( float, sprayHeightMax );
	DM_FIELD( uint, hasSprite );			// у клочка (froth) — спрайт (particles.ps, SRV 1), иначе узор пены
	DM_FIELD( float, selfShadow );		// клочок со спрайтом: плотность его массы для солнца (тень внутри массы)
	DM_FIELD( float, coreOcclusion );	// и доля света неба, которую не получает его ядро (0 — получает весь)
	DM_FIELD( float, sink );				// fall: всплывший клочок — центром ниже воды на эту долю своего полуразмера
	DM_FIELD( uint, drawPool );			// рисовать весь пул по порядку (froth), а не список живых кадра
	DM_FIELD( float, flatten );			// froth: доля вертикального размаха клочка (1 — квад к камере как есть)
	DM_FIELD( float, hold );				// fall: с рождения столько секунд течение клочок не несёт
	DM_FIELD( float, growTime );			// с рождения размер растёт от birthSize до sizeStart за столько секунд (0 — сразу)
	DM_FIELD( float, birthSize );		// м
	DM_FIELD( float, sizePadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
