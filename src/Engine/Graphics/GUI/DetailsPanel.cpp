#include "DetailsPanel.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "PropertyWidgets.h"
#include "EditHistory.h"

void DetailsPanel::draw( Editor::Entry* entry, bool* open, PropertyContainer* focus )
{
	if( !ImGui::Begin( "Details", open ) )
	{
		ImGui::End();
		return;
	}
	if( !entry )
	{
		ImGui::TextDisabled( "Select an object in the Outliner" );
		ImGui::End();
		return;
	}

	PropertyContainer& properties = *entry->properties;
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( properties.name().c_str() );
	if( properties.modified() )
	{
		const float button = ImGui::CalcTextSize( "Reset all" ).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - button );
		if( ImGui::Button( "Reset all" ) )
		{
			// Сброс окна целиком — одна правка истории
			Editor::EditHistory* history = PropertyWidgets::history();
			if( history )
				history->begin( "Reset all: " + properties.name(), Editor::EditHistory::collect( properties ) );
			properties.resetToSaved();
			if( history )
				history->end();
		}
	}
	if( entry->saved )
		ImGui::TextDisabled( "Saved with the level: File > Save level (Ctrl+S)" );
	else
		ImGui::TextDisabled( "Not saved: changes last until exit" );
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::InputTextWithHint( "##filter", "Search properties", &m_filter );
	ImGui::Separator();

	ImGui::BeginChild( "properties" );
	ImGui::PushID( &properties );
	PropertyWidgets::drawContainer( properties, m_filter, focus );
	ImGui::PopID();
	ImGui::EndChild();
	ImGui::End();
}
