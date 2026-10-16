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
	// Окно пресета окружения: правки — в активный пресет, остальные (эталон проверок) не меняются
	const ImVec4 presetColor( PropertyWidgets::presetColor[0], PropertyWidgets::presetColor[1], PropertyWidgets::presetColor[2],
							  PropertyWidgets::presetColor[3] );
	// Заметки — с переносом по ширине панели
	const auto note = []( const ImVec4& color, const std::string& text )
	{
		ImGui::PushStyleColor( ImGuiCol_Text, color );
		ImGui::TextWrapped( "%s", text.c_str() );
		ImGui::PopStyleColor();
	};
	const std::string preset = m_preset.empty() ? "Level" : m_preset;
	if( properties.presetPart() )
	{
		note( presetColor, "Preset " + preset + ": other presets keep their values (File > Preset)" );
		if( m_presetLocked )
			note( ImVec4( 1.0f, 0.45f, 0.4f, 1.0f ), "Locked: Save level skips it, use File > Preset > Save preset as" );
	}
	else
	{
		for( PropertyContainer* sub : properties.subContainer() )
		{
			if( sub->presetPart() )
			{
				note( presetColor, "Highlighted subwindows are in the preset " + preset );
				break;
			}
		}
	}
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
