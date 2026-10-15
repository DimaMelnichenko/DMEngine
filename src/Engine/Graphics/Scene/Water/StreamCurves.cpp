#include "StreamCurves.h"
#include <algorithm>
#include "Properties\SettingsProperties.h"

namespace GS
{

void StreamCurves::initialize( const WaterSimulationSettings& settings, PropertyContainer& window )
{
	// Русла и ручьи строит конвейер рельефа при загрузке — правки применяются при следующей загрузке уровня
	m_channelsProperties.setName( "Channels" );
	addSettingProperties( m_channelsProperties, waterChannelsSchema(), settings.channels );
	window.addSubContainer( &m_channelsProperties );

	// Кривые русел: подокно на кривую — включение и множители; правка помечает кривую правленной («Save level»):
	// новая генерация её не заменит
	m_streamsProperties.setName( "Streams" );
	m_streamProperties.clear();
	m_curves = settings.streams;
	m_builtCurves = settings.streams;
	m_builtChannels = settings.channels;
	m_initialChannels = settings.channels;
	m_curvesDirty = false;
	for( const StreamCurve& curve : m_curves )
	{
		auto container = std::make_unique<PropertyContainer>();
		container->setName( curve.name + ( curve.generated ? "" : " (manual)" ) + ( curve.edited ? " (edited)" : "" ) + " #" +
							std::to_string( curve.id ) );
		addSettingProperties( *container, streamCurveSchema(), curve );
		m_streamsProperties.addSubContainer( container.get() );
		m_streamProperties.push_back( std::move( container ) );
	}
	window.addSubContainer( &m_streamsProperties );
}

WaterChannelsSettings StreamCurves::channels() const
{
	WaterChannelsSettings channels = m_initialChannels;
	readSettingProperties( m_channelsProperties, waterChannelsSchema(), channels );
	return channels;
}

std::vector<StreamCurve> StreamCurves::streamCurves() const
{
	std::vector<StreamCurve> curves = m_curves;
	for( size_t i = 0; i < curves.size() && i < m_streamProperties.size(); ++i )
	{
		StreamCurve& curve = curves[i];
		// Правка в окне — кривая правленная: новая генерация её не заменит
		StreamCurve window;
		readSettingProperties( *m_streamProperties[i], streamCurveSchema(), window );
		curve.edited = curve.edited || !settingsEqual( streamCurveSchema(), window, curve );
		readSettingProperties( *m_streamProperties[i], streamCurveSchema(), curve );
	}
	return curves;
}

void StreamCurves::setCurvePoint( size_t curve, size_t point, DirectX::XMFLOAT2 position )
{
	if( curve < m_curves.size() && point < m_curves[curve].points.size() )
		m_curves[curve].points[point].position = position;
}

bool StreamCurves::insertCurvePoint( size_t curve, int after, DirectX::XMFLOAT2 position )
{
	if( curve >= m_curves.size() || after < -1 || after >= static_cast<int>( m_curves[curve].points.size() ) )
		return false;
	std::vector<StreamCurvePoint>& points = m_curves[curve].points;
	// Расход новой точки — как у соседней выше по течению (у первой — у прежней первой)
	StreamCurvePoint point = points[std::max( after, 0 )];
	point.position = position;
	points.insert( points.begin() + ( after + 1 ), point );
	finishCurveEdit( curve );
	return true;
}

bool StreamCurves::removeCurvePoint( size_t curve, size_t point )
{
	if( curve >= m_curves.size() || point >= m_curves[curve].points.size() || m_curves[curve].points.size() <= 2 )
		return false;
	m_curves[curve].points.erase( m_curves[curve].points.begin() + point );
	finishCurveEdit( curve );
	return true;
}

void StreamCurves::finishCurveEdit( size_t curve )
{
	if( curve >= m_curves.size() )
		return;
	m_curves[curve].edited = true;
	m_curves[curve].pointsChanged = true;
	m_curvesDirty = true;
}

PropertyContainer* StreamCurves::curveProperties( size_t curve )
{
	return curve < m_streamProperties.size() ? m_streamProperties[curve].get() : nullptr;
}

bool StreamCurves::rebuildRequested() const
{
	if( m_curvesDirty )
		return true;
	// Подокна «Channels» и «Streams» не правили с прошлой сверки — сверять нечего
	const uint64_t revision = std::max( m_channelsProperties.revision(), m_streamsProperties.revision() );
	if( revision == m_checkedRevision )
		return false;
	for( size_t i = 0; i < m_builtCurves.size() && i < m_streamProperties.size(); ++i )
	{
		StreamCurve window;
		readSettingProperties( *m_streamProperties[i], streamCurveSchema(), window );
		if( !settingsEqual( streamCurveSchema(), window, m_builtCurves[i] ) )
			return true;
	}
	// Параметры генератора кривых — при следующей загрузке (отпечаток), остальные — перестройка русел
	const WaterChannelsSettings current = channels();
	for( const SettingField<WaterChannelsSettings>& field : waterChannelsSchema() )
		if( field.group != streamGeneratorGroup && !settingFieldEqual( field, current, m_builtChannels ) )
			return true;
	m_checkedRevision = revision;
	return false;
}

void StreamCurves::markBuilt()
{
	m_builtCurves = streamCurves();
	m_builtChannels = channels();
	m_curvesDirty = false;
	m_checkedRevision = std::max( m_channelsProperties.revision(), m_streamsProperties.revision() );
}

}
