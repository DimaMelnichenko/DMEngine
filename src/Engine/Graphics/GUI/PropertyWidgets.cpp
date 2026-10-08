#include "PropertyWidgets.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include "imgui.h"

using namespace DirectX;

namespace PropertyWidgets
{

namespace
{

const ImVec4 modifiedColor( 1.0f, 0.75f, 0.3f, 1.0f );

// Формат числа: заданный у свойства или по величине значения — 128000 без экспоненты, 0,0003 — не в ноль
std::string numberFormat( const Property& property, float value )
{
	std::string format = property.format();
	if( format.empty() )
	{
		const float magnitude = std::fabs( value );
		if( magnitude == 0.0f || magnitude >= 100.0f )
			format = magnitude >= 1000.0f ? "%.0f" : "%.2f";
		else if( magnitude >= 1.0f )
			format = "%.3f";
		else if( magnitude >= 0.01f )
			format = "%.4f";
		else
			format = "%.3e";
	}
	if( !property.unit().empty() )
		format += " " + property.unit();
	return format;
}

// Шаг перетаскивания: доля диапазона, но не меньше доли самого значения
float dragSpeed( const Property& property, float value )
{
	const float range = property.high() - property.low();
	return std::max( { std::fabs( value ) * 0.005f, range > 0.0f ? range * 0.001f : 0.0f, 1e-5f } );
}

void labelCell( const std::string& name, Property& property )
{
	ImGui::TableNextColumn();
	ImGui::AlignTextToFramePadding();
	if( property.modified() )
		ImGui::TextColored( modifiedColor, "%s", name.c_str() );
	else
		ImGui::TextUnformatted( name.c_str() );
	if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
	{
		ImGui::BeginTooltip();
		ImGui::TextUnformatted( name.c_str() );
		if( !property.unit().empty() )
			ImGui::TextDisabled( "unit: %s", property.unit().c_str() );
		if( !property.tooltip().empty() )
		{
			ImGui::PushTextWrapPos( ImGui::GetFontSize() * 30.0f );
			ImGui::TextUnformatted( property.tooltip().c_str() );
			ImGui::PopTextWrapPos();
		}
		if( property.modified() )
			ImGui::TextColored( modifiedColor, "changed since load or save" );
		ImGui::EndTooltip();
	}
}

void controlCell( const std::string& name, Property& property )
{
	ImGui::TableNextColumn();
	ImGui::SetNextItemWidth( -FLT_MIN );
	const std::string id = "##" + name;
	const char* label = id.c_str();
	const ImGuiSliderFlags flags = property.logarithmic() ? ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat : 0;
	switch( property.valueType() )
	{
		case ValueType::BOOL:
			ImGui::Checkbox( label, property.dataPtr<bool>() );
			break;
		case ValueType::FLOAT:
		{
			float* value = property.dataPtr<float>();
			const std::string format = numberFormat( property, *value );
			switch( property.controlType() )
			{
				case GUIControlType::DRAG:
					ImGui::DragFloat( label, value, dragSpeed( property, *value ), property.low(), property.high(), format.c_str(), flags );
					break;
				case GUIControlType::LABEL:
					ImGui::Text( format.c_str(), *value );
					break;
				default:
					ImGui::SliderFloat( label, value, property.low(), property.high(), format.c_str(), flags );
					break;
			}
			break;
		}
		case ValueType::VECTOR2:
		{
			float* value = reinterpret_cast<float*>( property.dataPtr<XMFLOAT2>() );
			if( property.controlType() == GUIControlType::DRAG )
				ImGui::DragFloat2( label, value, dragSpeed( property, value[0] ), property.low(), property.high() );
			else
				ImGui::SliderFloat2( label, value, property.low(), property.high() );
			break;
		}
		case ValueType::VECTOR3:
		{
			float* value = reinterpret_cast<float*>( property.dataPtr<XMFLOAT3>() );
			switch( property.controlType() )
			{
				case GUIControlType::COLOR:
					ImGui::ColorEdit3( label, value, ImGuiColorEditFlags_Float );
					break;
				case GUIControlType::DRAG:
					ImGui::DragFloat3( label, value, dragSpeed( property, value[0] ), property.low(), property.high() );
					break;
				default:
					ImGui::SliderFloat3( label, value, property.low(), property.high() );
					break;
			}
			break;
		}
		case ValueType::VECTOR4:
		{
			float* value = reinterpret_cast<float*>( property.dataPtr<XMFLOAT4>() );
			switch( property.controlType() )
			{
				case GUIControlType::COLOR:
					ImGui::ColorEdit4( label, value, ImGuiColorEditFlags_Float );
					break;
				case GUIControlType::DRAG:
					ImGui::DragFloat4( label, value, dragSpeed( property, value[0] ), property.low(), property.high() );
					break;
				default:
					ImGui::SliderFloat4( label, value, property.low(), property.high() );
					break;
			}
			break;
		}
		case ValueType::INT:
		{
			int32_t* value = property.dataPtr<int32_t>();
			if( property.controlType() == GUIControlType::DRAG )
				ImGui::DragInt( label, value, 1.0f, static_cast<int>( property.low() ), static_cast<int>( property.high() ) );
			else
				ImGui::SliderInt( label, value, static_cast<int>( property.low() ), static_cast<int>( property.high() ) );
			break;
		}
		case ValueType::UINT:
			// Выбор текстуры по id пока не сделан
			ImGui::Text( "%u", property.data<uint32_t>() );
			break;
	}
}

void resetCell( const std::string& name, Property& property )
{
	ImGui::TableNextColumn();
	if( !property.modified() )
		return;
	ImGui::PushID( name.c_str() );
	if( ImGui::SmallButton( "reset" ) )
		property.resetToSaved();
	ImGui::PopID();
}

void drawProperties( PropertyContainer& container, const std::string& filter, bool containerMatches )
{
	bool any = false;
	for( const std::string& name : container.names() )
	{
		if( containerMatches || matches( name, filter ) )
		{
			any = true;
			break;
		}
	}
	if( !any )
		return;
	if( !ImGui::BeginTable( "properties", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable ) )
		return;
	ImGui::TableSetupColumn( "name", ImGuiTableColumnFlags_WidthStretch, 0.45f );
	ImGui::TableSetupColumn( "value", ImGuiTableColumnFlags_WidthStretch, 0.55f );
	ImGui::TableSetupColumn( "reset", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize( "reset" ).x + ImGui::GetStyle().FramePadding.x * 2.0f );
	for( const std::string& name : container.names() )
	{
		if( !containerMatches && !matches( name, filter ) )
			continue;
		Property& property = container.property( name );
		ImGui::TableNextRow();
		labelCell( name, property );
		controlCell( name, property );
		resetCell( name, property );
	}
	ImGui::EndTable();
}

void drawContainerImpl( PropertyContainer& container, const std::string& filter, bool parentMatches )
{
	const bool containerMatches = parentMatches || ( !filter.empty() && matches( container.name(), filter ) );
	drawProperties( container, filter, containerMatches );
	for( PropertyContainer* sub : container.subContainer() )
	{
		if( !containerMatches && !hasMatches( *sub, filter ) )
			continue;
		ImGui::PushID( sub );
		const std::string title = sub->modified() ? sub->name() + "  *" : sub->name();
		// Под фильтром — раскрыты: видно, что нашлось
		if( !filter.empty() )
			ImGui::SetNextItemOpen( true, ImGuiCond_Always );
		if( ImGui::CollapsingHeader( title.c_str() ) )
		{
			ImGui::Indent( ImGui::GetStyle().IndentSpacing * 0.5f );
			drawContainerImpl( *sub, filter, containerMatches );
			ImGui::Unindent( ImGui::GetStyle().IndentSpacing * 0.5f );
		}
		ImGui::PopID();
	}
}

}

bool matches( const std::string& text, const std::string& pattern )
{
	if( pattern.empty() )
		return true;
	auto lower = []( char c ) { return static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ); };
	return std::search( text.begin(), text.end(), pattern.begin(), pattern.end(),
						[&]( char a, char b ) { return lower( a ) == lower( b ); } ) != text.end();
}

bool hasMatches( PropertyContainer& container, const std::string& filter )
{
	if( filter.empty() || matches( container.name(), filter ) )
		return true;
	for( const std::string& name : container.names() )
	{
		if( matches( name, filter ) )
			return true;
	}
	for( PropertyContainer* sub : container.subContainer() )
	{
		if( hasMatches( *sub, filter ) )
			return true;
	}
	return false;
}

void drawContainer( PropertyContainer& container, const std::string& filter )
{
	drawContainerImpl( container, filter, false );
}

}
