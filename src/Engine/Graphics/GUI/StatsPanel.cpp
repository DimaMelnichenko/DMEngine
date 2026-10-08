#include "StatsPanel.h"
#include <algorithm>
#include <cstdio>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "PropertyWidgets.h"

namespace
{

bool endsWith( const std::string& text, const std::string& suffix )
{
	return text.size() >= suffix.size() && text.compare( text.size() - suffix.size(), suffix.size(), suffix ) == 0;
}

}

void StatsPanel::update( const GS::FrameStats& stats )
{
	m_timings.clear();
	m_counters.clear();
	auto timing = [this]( const std::string& name ) -> Timing&
	{
		auto it = std::find_if( m_timings.begin(), m_timings.end(), [&]( const Timing& t ) { return t.name == name; } );
		if( it != m_timings.end() )
			return *it;
		m_timings.push_back( { name } );
		return m_timings.back();
	};
	for( const auto& [format, value] : stats.counters )
	{
		// «имя = %.3f ms»: имя — до « = », остальное — формат значения
		const size_t separator = format.find( " = " );
		if( separator == std::string::npos )
			continue;
		const std::string name = format.substr( 0, separator );
		const std::string valueFormat = format.substr( separator + 3 );
		if( name == "GPU frame" )
		{
			m_gpuFrame = value;
			continue;
		}
		// Одноимённые области кадра (объект в нескольких проходах) — суммой
		if( endsWith( valueFormat, "ms" ) )
		{
			if( name.rfind( "GPU ", 0 ) == 0 )
			{
				Timing& t = timing( name.substr( 4 ) );
				t.gpu = std::max( t.gpu, 0.0f ) + value;
			}
			else
			{
				Timing& t = timing( name );
				t.cpu = std::max( t.cpu, 0.0f ) + value;
			}
		}
		else
			m_counters.push_back( { name, valueFormat, value } );
	}

	m_frameHistory[m_historyOffset] = ImGui::GetIO().DeltaTime * 1000.0f;
	m_gpuHistory[m_historyOffset] = m_gpuFrame;
	m_historyOffset = ( m_historyOffset + 1 ) % historySize;
}

void StatsPanel::drawOverlay( float cornerX, float cornerY )
{
	const float padding = 10.0f;
	ImGui::SetNextWindowPos( ImVec2( cornerX + padding, cornerY + padding ), ImGuiCond_Always );
	ImGui::SetNextWindowBgAlpha( 0.35f );
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
							 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
#ifdef IMGUI_HAS_DOCK
	flags |= ImGuiWindowFlags_NoDocking;
#endif
	if( ImGui::Begin( "##statsOverlay", nullptr, flags ) )
	{
		const ImGuiIO& io = ImGui::GetIO();
		ImGui::Text( "%.0f FPS   frame %.2f ms   GPU %.2f ms", io.Framerate, 1000.0f / std::max( io.Framerate, 1e-3f ), m_gpuFrame );
		float highest = 0.0f;
		for( int i = 0; i < historySize; ++i )
			highest = std::max( { highest, m_frameHistory[i], m_gpuHistory[i] } );
		const float scale = std::max( highest * 1.1f, 2.0f );
		const ImVec2 size( ImGui::GetFontSize() * 18.0f, ImGui::GetFontSize() * 3.0f );
		ImGui::PlotLines( "##frame", m_frameHistory, historySize, m_historyOffset, "CPU frame", 0.0f, scale, size );
		ImGui::PlotLines( "##gpu", m_gpuHistory, historySize, m_historyOffset, "GPU frame", 0.0f, scale, size );
	}
	ImGui::End();
}

void StatsPanel::draw( bool* open )
{
	if( !ImGui::Begin( "Stats", open ) )
	{
		ImGui::End();
		return;
	}
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::InputTextWithHint( "##filter", "Search", &m_filter );

	if( ImGui::CollapsingHeader( "Timings", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
									  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		if( ImGui::BeginTable( "timings", 3, flags ) )
		{
			ImGui::TableSetupColumn( "Pass / object", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_NoSort, 0.6f );
			ImGui::TableSetupColumn( "CPU ms", ImGuiTableColumnFlags_PreferSortDescending, 0.2f );
			ImGui::TableSetupColumn( "GPU ms", ImGuiTableColumnFlags_PreferSortDescending, 0.2f );
			ImGui::TableHeadersRow();
			std::vector<const Timing*> rows;
			for( const Timing& t : m_timings )
			{
				if( PropertyWidgets::matches( t.name, m_filter ) )
					rows.push_back( &t );
			}
			// Сортировка по колонке времени; без неё — в порядке кадра
			if( const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs() )
			{
				if( specs->SpecsCount > 0 && specs->Specs[0].ColumnIndex > 0 )
				{
					const int column = specs->Specs[0].ColumnIndex;
					const bool ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
					std::stable_sort( rows.begin(), rows.end(), [&]( const Timing* a, const Timing* b )
					{
						const float va = column == 1 ? a->cpu : a->gpu;
						const float vb = column == 1 ? b->cpu : b->gpu;
						return ascending ? va < vb : va > vb;
					} );
				}
			}
			for( const Timing* t : rows )
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( t->name.c_str() );
				ImGui::TableNextColumn();
				if( t->cpu >= 0.0f )
					ImGui::Text( "%.3f", t->cpu );
				ImGui::TableNextColumn();
				if( t->gpu >= 0.0f )
					ImGui::Text( "%.3f", t->gpu );
			}
			ImGui::EndTable();
		}
	}
	if( ImGui::CollapsingHeader( "Counters", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		if( ImGui::BeginTable( "counters", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp ) )
		{
			for( const Counter& c : m_counters )
			{
				if( !PropertyWidgets::matches( c.name, m_filter ) )
					continue;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( c.name.c_str() );
				ImGui::TableNextColumn();
				char value[64];
				std::snprintf( value, sizeof( value ), c.format.c_str(), c.value );
				ImGui::TextUnformatted( value );
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
}
