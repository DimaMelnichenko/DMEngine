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

// Формат числа: заданный у свойства или по величине значения — 128000 без экспоненты, 0,0003 — не в ноль; unit —
// с единицами (у поля ввода их нет)
std::string numberFormat( const Property& property, float value, bool unit = true )
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
	if( unit && !property.unit().empty() )
		format += " " + property.unit();
	return format;
}

// Ячейка «ползунок | поле ввода числа»: поле — под «-0000.000» (не уже «-0.000»), ползунок — остальное. В узкой
// ячейке (панель Camera) ползунку места нет — только поле. true — ползунок рисовать (ширина уже задана)
bool beginWithNumberField()
{
	const float padding = ImGui::GetStyle().FramePadding.x * 2.0f;
	const float avail = ImGui::GetContentRegionAvail().x;
	const float field = std::clamp( avail * 0.4f, ImGui::CalcTextSize( "-0.000" ).x + padding, ImGui::CalcTextSize( "-0000.000" ).x + padding );
	const float slider = avail - field - ImGui::GetStyle().ItemInnerSpacing.x;
	if( slider < ImGui::GetFontSize() * 2.5f )
		return false;
	ImGui::SetNextItemWidth( slider );
	return true;
}

// Поле ввода числа: точное значение без ползунка (за границы ползунка — можно); afterSlider — в той же строке
void numberField( const Property& property, float* value, bool afterSlider )
{
	if( afterSlider )
		ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemInnerSpacing.x );
	ImGui::SetNextItemWidth( -FLT_MIN );
	const std::string format = numberFormat( property, *value, !afterSlider );
	ImGui::InputFloat( "##number", value, 0.0f, 0.0f, format.c_str(), ImGuiInputTextFlags_CharsScientific );
}

void intField( int32_t* value, bool afterSlider )
{
	if( afterSlider )
		ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemInnerSpacing.x );
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::InputInt( "##number", value, 0, 0 );
}

// Шаг перетаскивания: доля диапазона, но не меньше доли самого значения
float dragSpeed( const Property& property, float value )
{
	if( property.dragSpeed() > 0.0f )
		return property.dragSpeed();
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
			ImGui::PushID( label );
			switch( property.controlType() )
			{
				case GUIControlType::DRAG:
				{
					const bool slider = beginWithNumberField();
					if( slider )
						ImGui::DragFloat( label, value, dragSpeed( property, *value ), property.low(), property.high(), format.c_str(), flags );
					numberField( property, value, slider );
					break;
				}
				case GUIControlType::LABEL:
					ImGui::Text( format.c_str(), *value );
					break;
				default:
				{
					const bool slider = beginWithNumberField();
					if( slider )
						ImGui::SliderFloat( label, value, property.low(), property.high(), format.c_str(), flags );
					numberField( property, value, slider );
					break;
				}
			}
			ImGui::PopID();
			break;
		}
		// Векторы — перетаскиванием (у ползунка — границы): двойной клик по полю — ввод числа
		case ValueType::VECTOR2:
		{
			float* value = reinterpret_cast<float*>( property.dataPtr<XMFLOAT2>() );
			ImGui::DragFloat2( label, value, dragSpeed( property, value[0] ), property.low(), property.high() );
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
				default:
					ImGui::DragFloat3( label, value, dragSpeed( property, value[0] ), property.low(), property.high() );
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
				default:
					ImGui::DragFloat4( label, value, dragSpeed( property, value[0] ), property.low(), property.high() );
					break;
			}
			break;
		}
		case ValueType::INT:
		{
			int32_t* value = property.dataPtr<int32_t>();
			ImGui::PushID( label );
			const bool slider = beginWithNumberField();
			if( slider && property.controlType() == GUIControlType::DRAG )
				ImGui::DragInt( label, value, 1.0f, static_cast<int>( property.low() ), static_cast<int>( property.high() ) );
			else if( slider )
				ImGui::SliderInt( label, value, static_cast<int>( property.low() ), static_cast<int>( property.high() ) );
			intField( value, slider );
			ImGui::PopID();
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

bool contains( PropertyContainer& container, const PropertyContainer* target )
{
	if( &container == target )
		return true;
	for( PropertyContainer* sub : container.subContainer() )
	{
		if( contains( *sub, target ) )
			return true;
	}
	return false;
}

void drawContainerImpl( PropertyContainer& container, const std::string& filter, bool parentMatches, PropertyContainer* focus )
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
		else if( focus )
			ImGui::SetNextItemOpen( contains( *sub, focus ), ImGuiCond_Always );
		const bool open = ImGui::CollapsingHeader( title.c_str() );
		if( sub == focus )
			ImGui::SetScrollHereY( 0.1f );
		if( open )
		{
			ImGui::Indent( ImGui::GetStyle().IndentSpacing * 0.5f );
			drawContainerImpl( *sub, filter, containerMatches, focus );
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

void drawContainer( PropertyContainer& container, const std::string& filter, PropertyContainer* focus )
{
	drawContainerImpl( container, filter, false, focus );
}

}
