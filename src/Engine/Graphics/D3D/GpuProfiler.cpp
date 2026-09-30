#include "GpuProfiler.h"
#include "DMD3D.h"

bool GpuProfiler::initialize()
{
	m_device = DMD3D::instance().GetDevice();
	m_context = DMD3D::instance().GetDeviceContext();

	// Метки событий есть у контекста D3D 11.1; без них профайлер просто меряет время
	ID3DUserDefinedAnnotation* annotation = nullptr;
	if( SUCCEEDED( m_context->QueryInterface( __uuidof( ID3DUserDefinedAnnotation ), reinterpret_cast<void**>( &annotation ) ) ) )
		m_annotation = make_com_ptr<ID3DUserDefinedAnnotation>( annotation );

	for( Frame& frame : m_frames )
	{
		if( !createQuery( D3D11_QUERY_TIMESTAMP_DISJOINT, frame.disjoint ) ||
			!createQuery( D3D11_QUERY_TIMESTAMP, frame.frameBegin ) ||
			!createQuery( D3D11_QUERY_TIMESTAMP, frame.frameEnd ) )
			return false;

		frame.timestamps.resize( maxScopes * 2 );
		for( auto& query : frame.timestamps )
		{
			if( !createQuery( D3D11_QUERY_TIMESTAMP, query ) )
				return false;
		}
	}

	return true;
}

bool GpuProfiler::createQuery( D3D11_QUERY type, com_unique_ptr<ID3D11Query>& query )
{
	D3D11_QUERY_DESC desc = {};
	desc.Query = type;
	ID3D11Query* raw = nullptr;
	if( FAILED( m_device->CreateQuery( &desc, &raw ) ) )
		return false;
	query = make_com_ptr<ID3D11Query>( raw );
	return true;
}

void GpuProfiler::beginFrame()
{
	// Кадр frameLatency кадров назад уже должен быть готов: забираем его и переиспользуем запросы
	Frame& frame = m_frames[m_current];
	collect( frame );

	frame.scopes.clear();
	frame.openScopes.clear();
	frame.usedTimestamps = 0;
	m_context->Begin( frame.disjoint.get() );
	m_context->End( frame.frameBegin.get() );
	m_frameOpen = true;
}

void GpuProfiler::endFrame()
{
	if( !m_frameOpen )
		return;

	Frame& frame = m_frames[m_current];
	m_context->End( frame.frameEnd.get() );
	m_context->End( frame.disjoint.get() );
	frame.pending = true;
	m_frameOpen = false;
	m_current = ( m_current + 1 ) % frameLatency;
}

void GpuProfiler::beginScope( const std::string& name )
{
	if( m_annotation )
		m_annotation->BeginEvent( utf8ToWide( name ).c_str() );

	Frame& frame = m_frames[m_current];
	if( !m_frameOpen || frame.usedTimestamps + 2 > frame.timestamps.size() )
	{
		frame.openScopes.push_back( UINT32_MAX );
		return;
	}

	Scope scope = { name, frame.usedTimestamps, frame.usedTimestamps + 1 };
	frame.usedTimestamps += 2;
	m_context->End( frame.timestamps[scope.begin].get() );
	frame.openScopes.push_back( static_cast<uint32_t>( frame.scopes.size() ) );
	frame.scopes.push_back( scope );
}

void GpuProfiler::endScope()
{
	Frame& frame = m_frames[m_current];
	if( !frame.openScopes.empty() )
	{
		const uint32_t index = frame.openScopes.back();
		frame.openScopes.pop_back();
		if( index != UINT32_MAX )
			m_context->End( frame.timestamps[frame.scopes[index].end].get() );
	}

	if( m_annotation )
		m_annotation->EndEvent();
}

uint64_t GpuProfiler::timestamp( ID3D11Query* query ) const
{
	uint64_t value = 0;
	m_context->GetData( query, &value, sizeof( value ), D3D11_ASYNC_GETDATA_DONOTFLUSH );
	return value;
}

void GpuProfiler::collect( Frame& frame )
{
	if( !frame.pending )
		return;

	// Не готово — оставляем прошлые результаты, а не ждём GPU
	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
	if( m_context->GetData( frame.disjoint.get(), &disjoint, sizeof( disjoint ), D3D11_ASYNC_GETDATA_DONOTFLUSH ) != S_OK )
		return;
	frame.pending = false;

	// Отметки читаем всегда: иначе при повторном использовании запросов debug-слой предупреждает о брошенных
	// результатах. Если частота менялась посреди кадра (энергосбережение), значения недостоверны и не показываются
	const bool valid = !disjoint.Disjoint && disjoint.Frequency != 0;
	const double toMilliseconds = valid ? 1000.0 / static_cast<double>( disjoint.Frequency ) : 0.0;

	const uint64_t frameBegin = timestamp( frame.frameBegin.get() );
	const uint64_t frameEnd = timestamp( frame.frameEnd.get() );
	std::vector<std::pair<std::string, float>> results;
	for( const Scope& scope : frame.scopes )
	{
		const uint64_t begin = timestamp( frame.timestamps[scope.begin].get() );
		const uint64_t end = timestamp( frame.timestamps[scope.end].get() );
		results.emplace_back( scope.name, static_cast<float>( ( end - begin ) * toMilliseconds ) );
	}

	if( valid )
	{
		m_frameMilliseconds = static_cast<float>( ( frameEnd - frameBegin ) * toMilliseconds );
		m_results = std::move( results );
	}
}
