#include "TerrainMasks.h"
#include "Utils\ValueNoise.h"
#include "TerrainFields.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <execution>
#include <filesystem>
#include <numeric>
#include <DirectXTex.h>

using namespace DirectX;

namespace GS
{

namespace
{

using Field = TerrainField;

float smoothstep( float a, float b, float x )
{
	return fieldSmoothstep( a, b, x );
}

float smoothstep( const TerrainLayerRules::Range& range, float x )
{
	return smoothstep( range.from, range.to, x );
}

// Поле по формуле в каждом текселе: fn( i ) → значение
template<typename Fn>
Field generate( uint32_t size, Fn fn )
{
	Field result( static_cast<size_t>( size ) * size );
	forRows( size, [&]( uint32_t row )
	{
		for( uint32_t col = 0; col < size; ++col )
		{
			const size_t i = static_cast<size_t>( row ) * size + col;
			result[i] = fn( i );
		}
	} );
	return result;
}

// Максимум по квадрату ( 2·radius + 1 )²: тонкие линии (русла) расширяются, а не гаснут, как при размытии
// Расстояние до ближайшей клетки воды (значение > 0,5), в текселях: два прохода фаски по восьми соседям (1 и √2, Borgefors
// 1986), у воды — 0; воды нет — очень большое
Field waterDistance( const Field& water, uint32_t size )
{
	const int n = static_cast<int>( size );
	const float diagonal = 1.41421356f;
	Field d( water.size() );
	for( size_t i = 0; i < d.size(); ++i )
		d[i] = water[i] > 0.5f ? 0.0f : 1e9f;
	auto at = [&]( int row, int col ) -> float& { return d[static_cast<size_t>( row ) * n + col]; };
	for( int row = 0; row < n; ++row )
		for( int col = 0; col < n; ++col )
		{
			float& v = at( row, col );
			if( col > 0 )
				v = std::min( v, at( row, col - 1 ) + 1.0f );
			if( row > 0 )
			{
				v = std::min( v, at( row - 1, col ) + 1.0f );
				if( col > 0 )
					v = std::min( v, at( row - 1, col - 1 ) + diagonal );
				if( col + 1 < n )
					v = std::min( v, at( row - 1, col + 1 ) + diagonal );
			}
		}
	for( int row = n - 1; row >= 0; --row )
		for( int col = n - 1; col >= 0; --col )
		{
			float& v = at( row, col );
			if( col + 1 < n )
				v = std::min( v, at( row, col + 1 ) + 1.0f );
			if( row + 1 < n )
			{
				v = std::min( v, at( row + 1, col ) + 1.0f );
				if( col + 1 < n )
					v = std::min( v, at( row + 1, col + 1 ) + diagonal );
				if( col > 0 )
					v = std::min( v, at( row + 1, col - 1 ) + diagonal );
			}
		}
	return d;
}

Field dilate( const Field& values, uint32_t size, int radius )
{
	const int n = static_cast<int>( size );
	Field temp( values.size() ), result( values.size() );
	forRows( size, [&]( uint32_t row )
	{
		for( int col = 0; col < n; ++col )
		{
			float peak = values[static_cast<size_t>( row ) * n + col];
			for( int k = -radius; k <= radius; ++k )
				peak = std::max( peak, values[static_cast<size_t>( row ) * n + std::clamp( col + k, 0, n - 1 )] );
			temp[static_cast<size_t>( row ) * n + col] = peak;
		}
	} );
	forRows( size, [&]( uint32_t row )
	{
		for( int col = 0; col < n; ++col )
		{
			float peak = temp[static_cast<size_t>( row ) * n + col];
			for( int k = -radius; k <= radius; ++k )
				peak = std::max( peak, temp[static_cast<size_t>( std::clamp( static_cast<int>( row ) + k, 0, n - 1 ) ) * n + col] );
			result[static_cast<size_t>( row ) * n + col] = peak;
		}
	} );
	return result;
}

// Сумма октав value noise — пары ( длина волны, м; вес ), нормированная на карте в [0, 1]. Длина волны — в метрах мира, а
// не в долях карты: на большой карте пятна того же размера
struct Octave
{
	float wavelength;
	float weight;
};

// scale — множитель длин волн всех октав (масштаб шума в окне правил)
Field fbm( uint32_t size, float texelSize, std::initializer_list<Octave> octaves, uint32_t seed, float scale = 1.0f )
{
	std::vector<Octave> list( octaves );
	for( Octave& octave : list )
		octave.wavelength *= std::max( scale, 1e-3f );
	Field result = generate( size, [&]( size_t i )
	{
		const float x = ( i % size ) * texelSize;
		const float y = ( i / size ) * texelSize;
		float total = 0.0f;
		uint32_t octaveSeed = seed;
		for( const Octave& octave : list )
			total += valueNoise( x / octave.wavelength, y / octave.wavelength, octaveSeed++ ) * octave.weight;
		return total;
	} );
	const auto [low, high] = std::minmax_element( result.begin(), result.end() );
	const float minimum = *low;
	const float range = std::max( *high - minimum, 1e-6f );
	for( float& value : result )
		value = ( value - minimum ) / range;
	return result;
}

// Радиус размытия из поля правил (тексели, дробное — к ближнему целому)
int blurRadius( float texels )
{
	return std::max( static_cast<int>( std::lround( texels ) ), 0 );
}

float at( const std::vector<float>* field, size_t i )
{
	return field && !field->empty() ? ( *field )[i] : 0.0f;
}

bool saveImage( const ScratchImage& image, const std::string& path )
{
	const std::wstring wide( path.begin(), path.end() );
	return SUCCEEDED( SaveToDDSFile( image.GetImages(), image.GetImageCount(), image.GetMetadata(), DDS_FLAGS_NONE, wide.c_str() ) );
}

constexpr const char* roleNames[terrainLayerRoleCount] = { "grass_dry", "talus", "rock", "snow", "grass_wet", "channel_bed",
															  "lake_gravel", "lake_silt" };

}

const char* terrainLayerRoleName( TerrainLayerRole role )
{
	return role < TerrainLayerRole::count ? roleNames[static_cast<uint32_t>( role )] : "";
}

TerrainLayerRole terrainLayerRole( const std::string& name )
{
	for( uint32_t role = 0; role < terrainLayerRoleCount; ++role )
		if( name == roleNames[role] )
			return static_cast<TerrainLayerRole>( role );
	return TerrainLayerRole::count;
}

bool TerrainMasks::build( const TerrainMaskInputs& inputs, const TerrainLayerRules& s, TerrainMasks& result )
{
	const HeightField& field = inputs.field;
	const uint32_t size = field.size;
	if( !field.heights || size < 3 )
		return false;
	const size_t cells = static_cast<size_t>( size ) * size;
	const float texel = field.texelSize;
	const float cellArea = texel * texel;
	const int n = static_cast<int>( size );
	result.size = size;

	// Высота — доли heightMultiplier, как исходная карта (правила — в долях высоты)
	const Field height = generate( size, [&]( size_t i ) { return field.heights[( i / size ) * field.rowPitch + i % size]; } );
	auto heightAt = [&]( int row, int col ) { return height[static_cast<size_t>( std::clamp( row, 0, n - 1 ) ) * n + std::clamp( col, 0, n - 1 )]; };

	// Уклон, градусы, по центральным разностям (за краем — крайний тексель), сглаженный: у мелких октав рельефа уклон
	// меняется через несколько текселей, и без сглаживания скала и камни рассыпаются штрихами. Север — к меньшим строкам
	// карты (вниз по картинке — на юг): склон смотрит на север, если к югу он выше — там снег лежит ниже
	const float gradientScale = field.heightMultiplier / ( 2.0f * texel );
	Field gradient( cells ), northRaw( cells ), slopeRaw( cells );
	forRows( size, [&]( uint32_t row )
	{
		const int r = static_cast<int>( row );
		for( int col = 0; col < n; ++col )
		{
			const size_t i = static_cast<size_t>( r ) * n + col;
			const float dx = ( heightAt( r, col + 1 ) - heightAt( r, col - 1 ) ) * gradientScale;
			const float dz = ( heightAt( r + 1, col ) - heightAt( r - 1, col ) ) * gradientScale;
			const float length = std::hypot( dx, dz );
			gradient[i] = length;
			slopeRaw[i] = std::atan( length ) * 57.29578f;
			northRaw[i] = std::clamp( dz / std::max( length, 1e-6f ), 0.0f, 1.0f ) * smoothstep( 5.0f, 15.0f, slopeRaw[i] );
		}
	} );
	const Field slope = blurField( slopeRaw, size, blurRadius( s.slopeBlur ) );
	const Field north = blurField( northRaw, size, blurRadius( s.slopeBlur ) );

	// Шумы: зерно — номер шума; длины волн — как прежние периоды сценария на карте 1 км
	const Field noise = generate( size, [&, base = fbm( size, texel, { { 128, 0.35f }, { 64, 0.3f }, { 32, 0.2f }, { 16, 0.1f }, { 8, 0.05f } }, 11,
															s.layerNoiseScale )]( size_t i ) { return base[i] - 0.5f; } );
	const Field patches = fbm( size, texel, { { 64, 0.5f }, { 32, 0.3f }, { 16, 0.2f } }, 21, s.layerNoiseScale );

	// Ветер — поле ветра по рельефу (TerrainWind, без леса): где ветер быстрее ветра уровня (перевалы, проходы, наветренные
	// склоны) — снег сдувает, где тише (подветренная тень за гребнем) — наметает. Гребни и ложбины — высота над рельефом,
	// сглаженным на ridgeRadius (TPI, м): мелкие гребни поле (слой воздуха ~100 м) не различает — с них сдувает всегда, в
	// ложбинах наметает в любом ветре; гребни, где ветер не тише ветра уровня (между началами наметания и сдувания), — для
	// леса
	Field exposure( cells, 0.0f ), deposit( cells, 0.0f ), ridgeExposure( cells, 0.0f );
	if( inputs.windSpeed && inputs.windSpeed->size() == cells )
	{
		const float metres = field.heightMultiplier;
		const Field& speed = *inputs.windSpeed;
		const Field smooth = blurField( height, size, blurRadius( s.ridgeRadius / texel ) );
		forRows( size, [&]( uint32_t row )
		{
			const int r = static_cast<int>( row );
			for( int col = 0; col < n; ++col )
			{
				const size_t i = static_cast<size_t>( r ) * n + col;
				const float h = height[i];
				const float scour = smoothstep( s.snowScourSpeed, speed[i] );
				const float drift = smoothstep( s.snowDriftSpeed, speed[i] );
				const float tpi = ( h - smooth[i] ) * metres;
				const float crest = smoothstep( s.ridge, tpi );
				const float hollow = smoothstep( s.hollow, tpi );
				exposure[i] = std::max( scour, crest );
				deposit[i] = std::max( drift * ( 1.0f - crest ), hollow );
				ridgeExposure[i] = crest * smoothstep( s.snowDriftSpeed.from, s.snowScourSpeed.from, speed[i] );
			}
		} );
	}

	// Водосбор: log10, м²; влажность — топографический индекс TWI = ln( a / tan β ) (Beven, Kirkby 1979; a — водосбор на
	// метр ширины склона): низины, дно долины, русла; плюс конусы выноса (отложения эрозии)
	const Field logFlow = generate( size, [&]( size_t i ) { return std::log10( std::max( at( inputs.catchment, i ), cellArea ) ); } );
	const Field wear = blurField( inputs.wear && !inputs.wear->empty() ? *inputs.wear : Field( cells, 0.0f ), size, blurRadius( s.erosionBlur ) );
	const Field talusMap = blurField( inputs.talus && !inputs.talus->empty() ? *inputs.talus : Field( cells, 0.0f ), size, blurRadius( s.erosionBlur ) );
	const Field gully = blurField( logFlow, size, 1 );
	const Field moisture = blurField( generate( size, [&]( size_t i )
	{
		const float twi = std::log( std::max( at( inputs.catchment, i ), cellArea ) / texel / std::max( gradient[i], 1e-3f ) );
		return std::max( smoothstep( s.moistureTwi, twi ), smoothstep( 0.1f, 0.5f, at( inputs.deposition, i ) ) );
	} ), size, blurRadius( s.moistureBlur ) );

	// Скала: крутые склоны и коренная порода там, где вода сняла грунт; снег — вершины, на северных склонах ниже; осыпи —
	// у подножий скал, крутоватые склоны, галька промоин на склонах (на дне долины — луг); остальное — трава
	Field rock( cells ), snow( cells ), boulders( cells ), grass( cells );
	forRows( size, [&]( uint32_t row )
	{
		for( uint32_t col = 0; col < size; ++col )
		{
			const size_t i = static_cast<size_t>( row ) * size + col;
			const float bedrock = smoothstep( s.bedrockWear, wear[i] ) * smoothstep( s.bedrockSlope, slope[i] );
			rock[i] = std::max( smoothstep( s.rockSlope, slope[i] + noise[i] * s.rockNoise ), s.bedrockShare * bedrock );
			snow[i] = smoothstep( s.snowHeight, height[i] + noise[i] * s.snowNoise + s.snowNorth * north[i] + s.snowDrift * deposit[i] -
											   s.snowScour * exposure[i] ) * ( 1.0f - s.rockClearsSnow * rock[i] );
			const float channel = smoothstep( s.gullyFlow, gully[i] ) * smoothstep( s.gullySlope, slope[i] );
			boulders[i] = std::max( { smoothstep( s.talusMap, talusMap[i] ), s.boulderShare * smoothstep( s.boulderSlope, slope[i] + noise[i] * s.boulderNoise ),
									  s.gullyShare * channel,
									  s.pebblePatchShare * smoothstep( s.pebblePatches, patches[i] ) * smoothstep( s.pebblePatchSlope, slope[i] ) } ) *
						  ( 1.0f - rock[i] ) * ( 1.0f - snow[i] );
			grass[i] = std::max( 1.0f - rock[i] - snow[i] - boulders[i], 0.0f );
			const float sum = std::max( rock[i] + snow[i] + boulders[i] + grass[i], 1e-6f );
			rock[i] /= sum;
			snow[i] /= sum;
			boulders[i] /= sum;
			grass[i] /= sum;
		}
	} );

	// Две травы: сочная — на влажном, суше — выше, граница — пятнами (~30–130 м); мягкий край — смешивание по высоте в шейдере
	const Field meadowNoise = fbm( size, texel, { { 128, 0.5f }, { 64, 0.3f }, { 32, 0.2f } }, 31, s.meadowNoiseScale );
	const auto wetness = [&]( size_t i )
	{
		return smoothstep( s.wetGrass, s.wetGrassMoisture * moisture[i] + ( 1.0f - s.wetGrassMoisture ) * meadowNoise[i] );
	};
	const Field byRole[terrainRuleRoleCount] = {
		generate( size, [&]( size_t i ) { return grass[i] * ( 1.0f - wetness( i ) ); } ),
		boulders,
		rock,
		snow,
		generate( size, [&]( size_t i ) { return grass[i] * wetness( i ); } ) };
	// Вес роли — в её слой; слоя с ролью нет — сухой траве, нет и её — слою 0 (сумма весов остаётся 1)
	for( Field& layer : result.splat )
		layer.clear();
	const int32_t fallback = inputs.layerOf[static_cast<uint32_t>( TerrainLayerRole::grassDry )] >= 0
								 ? inputs.layerOf[static_cast<uint32_t>( TerrainLayerRole::grassDry )] : 0;
	for( uint32_t role = 0; role < terrainRuleRoleCount; ++role )
	{
		const int32_t wanted = inputs.layerOf[role];
		const uint32_t layer = static_cast<uint32_t>( wanted >= 0 && wanted < static_cast<int32_t>( splatLayers ) ? wanted : fallback );
		Field& target = result.splat[layer];
		if( target.empty() )
			target = byRole[role];
		else
			for( size_t i = 0; i < cells; ++i )
				target[i] += byRole[role][i];
	}

	// Маски расстановки: трава — на обеих травах, ромашки — пятнами в траве, гуще на влажном, камни — на осыпях
	const Field flowerNoise = fbm( size, texel, { { 32, 0.5f }, { 16, 0.3f }, { 8, 0.2f } }, 41, s.meadowNoiseScale );
	result.masks.clear();
	result.masks.push_back( { "mask_grass", grass } );
	result.masks.push_back( { "mask_camomile", generate( size, [&]( size_t i )
	{
		return grass[i] * smoothstep( s.flowers, flowerNoise[i] + s.flowersMoisture * moisture[i] );
	} ) } );
	result.masks.push_back( { "mask_pebbles", boulders } );

	// Лес (как в Valley Benchmark: ель, сосна, берёза, кусты): не на скале, снегу и осыпях, не круче forestSlope, ниже границы
	// леса, не в руслах и озёрах (их даёт конвейер; без воды уровня — сток больше ~4000 м²), на дне долины — луга с рощами
	const float forestScale = s.forestNoiseScale;
	const Field forestNoise = generate( size, [&, base = fbm( size, texel, { { 128, 0.4f }, { 64, 0.3f }, { 32, 0.2f }, { 16, 0.1f } }, 51,
															  forestScale )]( size_t i ) { return base[i] - 0.5f; } );
	const bool hasWater = inputs.water && !inputs.water->empty();
	const Field stream = hasWater ? blurField( dilate( *inputs.water, size, 1 ), size, 1 )
								  : generate( size, [&, flow = blurField( dilate( logFlow, size, 1 ), size, 1 )]( size_t i ) { return smoothstep( 3.6f, 4.2f, flow[i] ); } );
	// Берега — полоса в несколько метров по обе стороны русла (ольха, ива)
	const int bankWidth = blurRadius( s.bankWidth );
	const Field banksRaw = hasWater ? blurField( dilate( *inputs.water, size, bankWidth ), size, 2 ) : blurField( dilate( logFlow, size, bankWidth ), size, 2 );
	// Расстояние до воды (без воды конвейера — до полосы русел по стоку), м: отступ кустов и травы у воды
	const Field toWater = waterDistance( hasWater ? *inputs.water : stream, size );
	const Field clearingNoise = fbm( size, texel, { { 64, 0.5f }, { 32, 0.3f }, { 16, 0.2f } }, 61, forestScale );
	const Field stands[3] = { fbm( size, texel, { { 128, 0.5f }, { 64, 0.3f }, { 32, 0.2f } }, 71, forestScale ),
							  fbm( size, texel, { { 128, 0.5f }, { 64, 0.3f }, { 32, 0.2f } }, 81, forestScale ),
							  fbm( size, texel, { { 128, 0.5f }, { 64, 0.3f }, { 32, 0.2f } }, 91, forestScale ) };
	const Field riparianNoise = fbm( size, texel, { { 32, 0.5f }, { 16, 0.5f } }, 101, forestScale );
	const Field shrubPatches = fbm( size, texel, { { 32, 0.5f }, { 16, 0.3f }, { 8, 0.2f } }, 111, forestScale );
	Field spruce( cells ), pine( cells ), birch( cells ), riparian( cells ), riparianGrass( cells ), shrubsSlope( cells );
	forRows( size, [&]( uint32_t row )
	{
		for( uint32_t col = 0; col < size; ++col )
		{
			const size_t i = static_cast<size_t>( row ) * size + col;
			const float h = height[i];
			const float treelineHeight = h + s.treelineNoise * forestNoise[i] + s.treelineNorth * north[i] + s.windTreeline * ridgeExposure[i];
			const float treeline = 1.0f - smoothstep( s.treeline, treelineHeight );
			const float ground = ( 1.0f - rock[i] ) * ( 1.0f - snow[i] ) * ( 1.0f - s.stonesClearForest * smoothstep( 0.45f, 0.85f, boulders[i] ) );
			const float steep = 1.0f - smoothstep( s.forestSlope, slope[i] + s.forestSlopeNoise * forestNoise[i] );
			const float floor = smoothstep( s.valleyFloorHeight, h ) * smoothstep( s.valleyFloorSlope, slope[i] );
			const float clearings = smoothstep( s.clearings, clearingNoise[i] - s.valleyClearings * floor );
			const float forest = ground * steep * treeline * ( 1.0f - stream[i] ) * clearings * ( 1.0f - s.windThinsForest * ridgeExposure[i] );

			// Типы по месту: ель — выше и на северных склонах, сосна — на средних сухих склонах к солнцу, берёза — во влажных
			// низинах; у каждого свой шум древостоев (~30–130 м), степень 3 — чистые древостои с узкой смешанной полосой
			float types[3] = {
				( 0.35f + 0.65f * smoothstep( s.spruceHeight, h + 0.1f * forestNoise[i] ) ) * ( 0.7f + 0.3f * north[i] ) * ( 0.4f + 1.2f * stands[0][i] ),
				smoothstep( s.pineSlope, slope[i] ) * ( 1.1f - moisture[i] ) * ( 1.0f - 0.6f * north[i] ) * smoothstep( s.pineHeight, h ) *
					( 0.4f + 1.2f * stands[1][i] ),
				smoothstep( s.birchMoisture, moisture[i] ) * smoothstep( s.birchHeight, h ) * smoothstep( s.birchSlope, slope[i] ) * ( 0.4f + 1.2f * stands[2][i] ) };
			float total = 0.0f;
			for( float& type : types )
			{
				type = std::pow( std::max( type, 0.0f ) + 1e-4f, s.standContrast );
				total += type;
			}
			spruce[i] = forest * types[0] / total;
			pine[i] = forest * types[1] / total;
			birch[i] = forest * types[2] / total;

			// Кусты: у русел — полоса шире самого русла; на склонах — стланик над границей леса и прогалины сухих склонов
			const float banks = ( hasWater ? smoothstep( 0.25f, 0.75f, banksRaw[i] ) : smoothstep( 3.5f, 3.9f, banksRaw[i] ) ) * ground *
								smoothstep( s.riparianHeight, h );
			// Отступ от воды — параметром у каждой маски: от 0 у воды до полной на gap м (0 — до самого уреза: в воде и в русле
			// растения убирает расстановка по глубине в точке и по врезу 0,25 м, scatter.cs, foliageClear). Прежде у обеих — по
			// клеткам 1 м (размытая полоса русла и клетки опускания), у травы это оставляло 2–3 м голой земли у кромки (2026-10-13)
			const float distance = toWater[i] * texel;
			const auto gapFade = [distance]( float gap ) { return gap > 0.0f ? smoothstep( 0.0f, gap, distance ) : 1.0f; };
			riparian[i] = banks * gapFade( s.bankShrubsGap ) * smoothstep( s.riparianDensity, riparianNoise[i] + 0.2f * moisture[i] );
			// Трава у воды — та же полоса берегов, но сплошная: шум её только прореживает
			riparianGrass[i] = banks * gapFade( s.bankGrassGap ) *
							   ( 0.5f + 0.5f * smoothstep( s.riparianGrass, riparianNoise[i] + 0.2f * moisture[i] ) );
			const float krummholz = smoothstep( s.krummholzHeight, treelineHeight ) * smoothstep( s.krummholzTop, h );
			const float dryGaps = ( 1.0f - clearings ) * smoothstep( s.dryGapsSlope, slope[i] ) * ( 1.0f - moisture[i] ) * ( 1.0f - floor );
			shrubsSlope[i] = std::max( krummholz * smoothstep( 0.40f, 0.60f, shrubPatches[i] ), s.dryGapsShare * dryGaps ) * ground * steep * ( 1.0f - stream[i] );
		}
	} );
	result.masks.push_back( { "mask_forest_spruce", std::move( spruce ) } );
	result.masks.push_back( { "mask_forest_pine", std::move( pine ) } );
	result.masks.push_back( { "mask_forest_birch", std::move( birch ) } );
	result.masks.push_back( { "mask_shrubs_riparian", std::move( riparian ) } );
	result.masks.push_back( { "mask_grass_riparian", std::move( riparianGrass ) } );
	result.masks.push_back( { "mask_shrubs_slope", std::move( shrubsSlope ) } );
	// Открытость ветру — для просмотра и тех, кому она пригодится (снег, частицы, ветер по рельефу)
	result.masks.push_back( { windExposureMask, std::move( exposure ) } );
	return true;
}

const char* TerrainMasks::codeStamp()
{
	return __DATE__ " " __TIME__;
}

void TerrainMasks::quantize()
{
	auto round8 = []( std::vector<float>& values )
	{
		for( float& value : values )
			value = static_cast<float>( std::lround( std::clamp( value, 0.0f, 1.0f ) * 255.0f ) ) / 255.0f;
	};
	for( std::vector<float>& layer : splat )
		round8( layer );
	for( Mask& mask : masks )
		round8( mask.values );
}

bool TerrainMasks::load( const std::string& directory, uint32_t expectedSize, const std::vector<std::string>& names )
{
	auto read = [expectedSize]( const std::string& path, DXGI_FORMAT format, size_t slices, ScratchImage& image )
	{
		const std::wstring wide( path.begin(), path.end() );
		if( FAILED( LoadFromDDSFile( wide.c_str(), DDS_FLAGS_NONE, nullptr, image ) ) )
			return false;
		const TexMetadata& meta = image.GetMetadata();
		return meta.format == format && meta.width == expectedSize && meta.height == expectedSize && meta.arraySize == slices;
	};
	const size_t cells = static_cast<size_t>( expectedSize ) * expectedSize;
	ScratchImage splatImage;
	constexpr size_t slices = splatLayers / 4;
	if( !read( directory + "\\splatmap.dds", DXGI_FORMAT_R8G8B8A8_UNORM, slices, splatImage ) )
		return false;
	TerrainMasks loaded;
	loaded.size = expectedSize;
	for( size_t slice = 0; slice < slices; ++slice )
	{
		const Image& image = *splatImage.GetImage( 0, slice, 0 );
		for( size_t c = 0; c < 4; ++c )
		{
			std::vector<float>& layer = loaded.splat[slice * 4 + c];
			layer.resize( cells );
			bool used = false;
			for( uint32_t y = 0; y < expectedSize; ++y )
			{
				const uint8_t* texel = image.pixels + y * image.rowPitch + c;
				for( uint32_t x = 0; x < expectedSize; ++x, texel += 4 )
				{
					layer[static_cast<size_t>( y ) * expectedSize + x] = *texel / 255.0f;
					used = used || *texel != 0;
				}
			}
			if( !used )
				layer.clear();
		}
	}
	for( const std::string& name : names )
	{
		ScratchImage maskImage;
		if( !read( directory + "\\" + name + ".dds", DXGI_FORMAT_R8_UNORM, 1, maskImage ) )
			return false;
		const Image& image = *maskImage.GetImage( 0, 0, 0 );
		Mask mask{ name, std::vector<float>( cells ) };
		for( uint32_t y = 0; y < expectedSize; ++y )
			for( uint32_t x = 0; x < expectedSize; ++x )
				mask.values[static_cast<size_t>( y ) * expectedSize + x] = image.pixels[y * image.rowPitch + x] / 255.0f;
		loaded.masks.push_back( std::move( mask ) );
	}
	*this = std::move( loaded );
	return true;
}

bool TerrainMasks::save( const std::string& directory ) const
{
	if( size == 0 )
		return false;
	std::error_code error;
	std::filesystem::create_directories( directory, error );
	constexpr size_t slices = splatLayers / 4;
	ScratchImage splatImage;
	if( FAILED( splatImage.Initialize2D( DXGI_FORMAT_R8G8B8A8_UNORM, size, size, slices, 1 ) ) )
		return false;
	for( size_t slice = 0; slice < slices; ++slice )
	{
		const Image& image = *splatImage.GetImage( 0, slice, 0 );
		for( uint32_t y = 0; y < size; ++y )
		{
			uint8_t* texel = image.pixels + y * image.rowPitch;
			for( uint32_t x = 0; x < size; ++x, texel += 4 )
				for( size_t c = 0; c < 4; ++c )
				{
					const std::vector<float>& layer = splat[slice * 4 + c];
					texel[c] = layer.empty() ? 0 : static_cast<uint8_t>( std::lround( std::clamp( layer[static_cast<size_t>( y ) * size + x], 0.0f, 1.0f ) * 255.0f ) );
				}
		}
	}
	bool saved = saveImage( splatImage, directory + "\\splatmap.dds" );
	for( const Mask& mask : masks )
	{
		ScratchImage image;
		if( FAILED( image.Initialize2D( DXGI_FORMAT_R8_UNORM, size, size, 1, 1 ) ) )
			return false;
		const Image& target = *image.GetImage( 0, 0, 0 );
		for( uint32_t y = 0; y < size; ++y )
			for( uint32_t x = 0; x < size; ++x )
				target.pixels[y * target.rowPitch + x] =
					static_cast<uint8_t>( std::lround( std::clamp( mask.values[static_cast<size_t>( y ) * size + x], 0.0f, 1.0f ) * 255.0f ) );
		saved = saveImage( image, directory + "\\" + mask.name + ".dds" ) && saved;
	}
	return saved;
}

std::string TerrainMasks::summary() const
{
	std::string text = "layers (share of the map):";
	char item[96];
	for( uint32_t layer = 0; layer < splatLayers; ++layer )
	{
		if( splat[layer].empty() )
			continue;
		const double sum = std::accumulate( splat[layer].begin(), splat[layer].end(), 0.0 );
		std::snprintf( item, sizeof( item ), " %u %.1f%%", layer, 100.0 * sum / splat[layer].size() );
		text += item;
	}
	text += "; masks (covered > 0.5):";
	for( const Mask& mask : masks )
	{
		const size_t covered = std::count_if( mask.values.begin(), mask.values.end(), []( float value ) { return value > 0.5f; } );
		std::snprintf( item, sizeof( item ), " %s %.1f%%", mask.name.c_str() + 5, 100.0 * covered / std::max<size_t>( mask.values.size(), 1 ) );
		text += item;
	}
	return text;
}

}
