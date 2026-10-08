#include "OutlinerPanel.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "PropertyWidgets.h"
#include "SceneObject.h"

void OutlinerPanel::draw( std::vector<Editor::Entry>& entries, int& selected, bool* open )
{
	if( !ImGui::Begin( "Outliner", open ) )
	{
		ImGui::End();
		return;
	}
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::InputTextWithHint( "##filter", "Search", &m_filter );

	static const std::pair<Editor::Category, const char*> sections[] = {
		{ Editor::Category::environment, "Environment" }, { Editor::Category::scene, "Scene" }, { Editor::Category::rendering, "Rendering" } };
	for( const auto& [category, title] : sections )
	{
		ImGui::SetNextItemOpen( true, ImGuiCond_FirstUseEver );
		if( !ImGui::CollapsingHeader( title ) )
			continue;
		for( int i = 0; i < static_cast<int>( entries.size() ); ++i )
		{
			Editor::Entry& entry = entries[i];
			if( entry.category != category || !PropertyWidgets::matches( entry.properties->name(), m_filter ) )
				continue;
			ImGui::PushID( i );
			// Видимость объекта сцены — как глаз в Outliner UE
			if( entry.object )
			{
				bool visible = entry.object->visible();
				if( ImGui::Checkbox( "##visible", &visible ) )
					entry.object->setVisible( visible );
				if( ImGui::IsItemHovered() )
					ImGui::SetTooltip( "Visible" );
				ImGui::SameLine();
			}
			else
			{
				ImGui::Dummy( ImVec2( ImGui::GetFrameHeight(), ImGui::GetFrameHeight() ) );
				ImGui::SameLine();
			}
			const std::string label = entry.properties->modified() ? entry.properties->name() + "  *" : entry.properties->name();
			ImGui::AlignTextToFramePadding();
			if( ImGui::Selectable( label.c_str(), selected == i ) )
				selected = i;
			ImGui::PopID();
		}
	}
	ImGui::End();
}
