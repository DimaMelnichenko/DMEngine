#include "TextureLibraryPanel.h"
#include <algorithm>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "System.h"
#include "PropertyWidgets.h"

void TextureLibraryPanel::draw( bool* open )
{
	if( !ImGui::Begin( "Texture Library", open ) )
	{
		ImGui::End();
		return;
	}
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.6f );
	ImGui::InputTextWithHint( "##filter", "Search", &m_filter );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::SliderFloat( "##size", &m_thumbnail, 48.0f, 256.0f, "%.0f px" );

	ImGui::BeginChild( "textures" );
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const int columns = std::max( 1, static_cast<int>( ( ImGui::GetContentRegionAvail().x + spacing ) / ( m_thumbnail + spacing ) ) );
	int column = 0;
	for( auto& item : GS::System::textures() )
	{
		const std::string& name = item.second->name();
		if( !PropertyWidgets::matches( name, m_filter ) )
			continue;
		if( column > 0 )
			ImGui::SameLine();
		ImGui::BeginGroup();
		// ImTextureID у бэкенда imgui_impl_dx12 — GPU-дескриптор вида из общей кучи
		const ImTextureID texture = item.second->srv().valid() ? static_cast<ImTextureID>( item.second->srv().descriptor().gpu.ptr ) : 0;
		if( texture )
			ImGui::Image( texture, ImVec2( m_thumbnail, m_thumbnail ) );
		else
			ImGui::Dummy( ImVec2( m_thumbnail, m_thumbnail ) );
		if( ImGui::IsItemHovered() )
		{
			ImGui::BeginTooltip();
			ImGui::Text( "%s  (id %u, %u x %u)", name.c_str(), item.first, item.second->width(), item.second->height() );
			if( texture )
				ImGui::Image( texture, ImVec2( 384.0f, 384.0f ) );
			ImGui::EndTooltip();
		}
		ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + m_thumbnail );
		ImGui::TextUnformatted( name.c_str() );
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
		column = ( column + 1 ) % columns;
	}
	ImGui::EndChild();
	ImGui::End();
}
