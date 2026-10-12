#include "PropertyWidgets.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include "imgui.h"
#include "imgui_internal.h"
#include "EditHistory.h"

using namespace DirectX;

namespace PropertyWidgets
{

namespace
{

const ImVec4 modifiedColor( 1.0f, 0.75f, 0.3f, 1.0f );

// Свойство, которое правят (активный контрол), и рисовался ли его контрол в этом кадре — endFrame
Property* editedProperty = nullptr;
bool editedPropertyDrawn = false;
Editor::EditHistory* editHistory = nullptr;
// Нажатая кнопка окна — до takePressedButton
PropertyContainer* pressedContainer = nullptr;
std::string pressedButton;

// Правка идёт, пока контрол свойства активен (у числа — ползунок или поле ввода: группа)
void markEditing( Property& property, bool active )
{
	property.setEditing( active );
	if( active )
	{
		if( editedProperty && editedProperty != &property )
			editedProperty->setEditing( false );
		editedProperty = &property;
		editedPropertyDrawn = true;
	}
	else if( editedProperty == &property )
		editedProperty = nullptr;
}

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

// Шаг стрелок и привязки (Ctrl) без заданного у свойства: степень десяти около трёх пикселей перетаскивания — у доли 0…1
// на поле в 200 пикселей 0,01, у метров 0…200 — 1
double stepFor( double speed )
{
	return std::pow( 10.0, std::floor( std::log10( std::max( speed * 3.0, 1e-9 ) ) ) );
}

// Привязка Ctrl: перетаскивание копится в «сыром» значении поля, которое сейчас тянут, а на поле — оно, округлённое до
// кратного шагу. Округлять само значение нельзя: ImGui прибавляет к нему движение мыши, и мелкие движения округлялись бы
// обратно — значение стояло бы на месте
ImGuiID snapItem = 0;
double snapRaw = 0.0;

// Общее у чисел: фон и заливка под полем, поле ImGui (DragScalar с вводом по клику — io.ConfigDragClickToInputText),
// стрелки при наведении
template<class TYPE>
bool numberFieldImpl( const char* id, TYPE* value, ImGuiDataType type, const char* format, const NumberOptions& options )
{
	const ImGuiIO& io = ImGui::GetIO();
	const ImGuiStyle& style = ImGui::GetStyle();
	const float width = ImGui::CalcItemWidth();
	const ImVec2 min = ImGui::GetCursorScreenPos();
	const ImVec2 max( min.x + width, min.y + ImGui::GetFrameHeight() );
	const double low = options.low;
	const double high = options.high;
	const bool bounded = high > low;
	const bool integer = type == ImGuiDataType_S32;
	double speed = options.speed > 0.0f ? options.speed :
				   bounded ? ( high - low ) / std::max( width, 1.0f ) : std::max( std::fabs( static_cast<double>( *value ) ) * 0.005, 1e-4 );
	const double autoStep = options.step > 0.0f ? options.step : stepFor( speed );
	const double step = integer ? std::max( 1.0, std::round( autoStep ) ) : autoStep;
	if( io.KeyShift )
		speed *= 0.1;
	if( integer )
		speed = std::max( speed, 0.05 );

	// Фон поля и заливка доли — под ним: у самого поля фон прозрачный, подсветка наведения и правки — поверх
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled( min, max, ImGui::GetColorU32( ImGuiCol_FrameBg ), style.FrameRounding );
	if( options.fill && bounded )
	{
		const double v = static_cast<double>( *value );
		double share = options.logarithmic && low > 0.0 ? std::log( std::max( v, low ) / low ) / std::log( high / low ) : ( v - low ) / ( high - low );
		share = std::clamp( share, 0.0, 1.0 );
		if( share > 0.0 )
			draw->AddRectFilled( min, ImVec2( min.x + static_cast<float>( width * share ), max.y ),
								 ImGui::GetColorU32( ImGuiCol_SliderGrab, 0.45f ), style.FrameRounding );
	}
	ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImGui::GetColorU32( ImGuiCol_Text, 0.06f ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImGui::GetColorU32( ImGuiCol_Text, 0.10f ) );
	const TYPE before = *value;
	const TYPE typedLow = static_cast<TYPE>( low );
	const TYPE typedHigh = static_cast<TYPE>( high );
	ImGuiSliderFlags flags = ImGuiSliderFlags_NoSpeedTweaks;
	if( options.logarithmic )
		flags |= ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat;
	ImGui::SetNextItemAllowOverlap();
	ImGui::SetNextItemWidth( width );
	bool changed = ImGui::DragScalar( id, type, value, static_cast<float>( speed ), bounded ? &typedLow : nullptr, bounded ? &typedHigh : nullptr,
									  format, flags );
	ImGui::PopStyleColor( 3 );
	const ImGuiID item = ImGui::GetItemID();
	if( ImGui::IsItemActive() && !ImGui::TempInputIsActive( item ) )
	{
		if( snapItem != item )
		{
			snapItem = item;
			snapRaw = static_cast<double>( before );
		}
		snapRaw += static_cast<double>( *value ) - static_cast<double>( before );
		double v = io.KeyCtrl ? std::round( snapRaw / step ) * step : snapRaw;
		if( bounded )
			v = std::clamp( v, low, high );
		const TYPE snapped = static_cast<TYPE>( integer ? std::round( v ) : v );
		changed = changed || snapped != *value;
		*value = snapped;
	}
	else if( snapItem == item )
		snapItem = 0;
	// Стрелки — кнопки поверх поля: пока одну из них держат, она активна, и поле «под курсором» только с этим разрешением
	ImGui::PushID( id );
	const ImGuiID less = ImGui::GetID( "##less" );
	const ImGuiID more = ImGui::GetID( "##more" );
	ImGui::PopID();
	const ImGuiID active = ImGui::GetActiveID();
	const bool hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenOverlappedByItem | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem ) &&
						 ( active == 0 || active == item || active == less || active == more );
	const bool textInput = ImGui::TempInputIsActive( item );
	if( textInput || !hovered || ImGui::IsItemActive() )
	{
		if( hovered && !textInput )
			ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
		return changed;
	}

	// Стрелки по краям — шаг вниз и вверх: кнопки поверх поля (у поля AllowOverlap) по своему прямоугольнику, без
	// раскладки — курсор не двигается (как крестик вкладки в ImGui; SetCursorScreenPos туда и обратно без элемента после
	// него ImGui считает попыткой раздвинуть ячейку)
	const float arrow = ImGui::GetFrameHeight() * 0.8f;
	bool arrowHovered = false;
	for( int side = 0; side < 2; ++side )
	{
		const ImVec2 at( side == 0 ? min.x : max.x - arrow, min.y );
		const ImRect rect( at, ImVec2( at.x + arrow, max.y ) );
		const ImGuiID arrowId = side == 0 ? less : more;
		ImGui::ItemAdd( rect, arrowId );
		bool over = false;
		bool held = false;
		if( ImGui::ButtonBehavior( rect, arrowId, &over, &held ) )
		{
			double v = static_cast<double>( *value ) + ( side == 0 ? -step : step );
			if( bounded )
				v = std::clamp( v, low, high );
			*value = static_cast<TYPE>( integer ? std::round( v ) : v );
			changed = true;
			ImGui::MarkItemEdited( item );
		}
		arrowHovered = arrowHovered || over;
		const float size = ImGui::GetFontSize() * 0.55f;
		const ImVec2 center( at.x + arrow * 0.5f, ( min.y + max.y ) * 0.5f );
		const ImU32 color = ImGui::GetColorU32( ImGuiCol_Text, over ? 1.0f : 0.5f );
		if( side == 0 )
			draw->AddTriangleFilled( ImVec2( center.x - size * 0.4f, center.y ), ImVec2( center.x + size * 0.3f, center.y - size * 0.5f ),
									 ImVec2( center.x + size * 0.3f, center.y + size * 0.5f ), color );
		else
			draw->AddTriangleFilled( ImVec2( center.x + size * 0.4f, center.y ), ImVec2( center.x - size * 0.3f, center.y + size * 0.5f ),
									 ImVec2( center.x - size * 0.3f, center.y - size * 0.5f ), color );
	}
	ImGui::SetMouseCursor( arrowHovered ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeEW );
	return changed;
}

// Шаг перетаскивания: доля диапазона, но не меньше доли самого значения
float dragSpeed( const Property& property, float value )
{
	if( property.dragSpeed() > 0.0f )
		return property.dragSpeed();
	const float range = property.high() - property.low();
	return std::max( { std::fabs( value ) * 0.005f, range > 0.0f ? range * 0.001f : 0.0f, 1e-5f } );
}

// Векторы и полоса: тот же шаг, Shift — вдесятеро точнее (как у чисел, numberField)
float vectorSpeed( const Property& property, float value )
{
	return dragSpeed( property, value ) * ( ImGui::GetIO().KeyShift ? 0.1f : 1.0f );
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

// owner — имя окна или подокна свойства: так правка называется в истории («Meadow / 0: GrassClump + 3: Far border»)
void controlCell( const std::string& name, Property& property, const std::string& owner )
{
	ImGui::TableNextColumn();
	// Группа: у числа ползунок и поле ввода — один контрол для IsItemActive (правка идёт, пока активен любой)
	ImGui::BeginGroup();
	ImGui::SetNextItemWidth( -FLT_MIN );
	const std::string id = "##" + name;
	const char* label = id.c_str();
	// Векторы и полоса — перетаскиванием, клик — ввод (io.ConfigDragClickToInputText); Shift — точнее, как у чисел
	const ImGuiSliderFlags flags = ImGuiSliderFlags_NoSpeedTweaks |
								   ( property.logarithmic() ? ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat : 0 );
	switch( property.valueType() )
	{
		case ValueType::BOOL:
			ImGui::Checkbox( label, property.dataPtr<bool>() );
			break;
		case ValueType::FLOAT:
		{
			float* value = property.dataPtr<float>();
			const std::string format = numberFormat( property, *value );
			if( property.controlType() == GUIControlType::LABEL )
			{
				ImGui::Text( format.c_str(), *value );
				break;
			}
			// Числовое поле как в Blender (numberField): у SLIDER — заливка доли диапазона, весь диапазон на ширину поля;
			// у DRAG — шаг по величине значения
			NumberOptions options;
			options.low = property.low();
			options.high = property.high();
			options.fill = property.controlType() != GUIControlType::DRAG;
			options.speed = property.controlType() == GUIControlType::DRAG ? dragSpeed( property, *value ) : 0.0f;
			options.step = property.step();
			options.logarithmic = property.logarithmic();
			numberField( label, value, format.c_str(), options );
			break;
		}
		// Векторы — перетаскиванием (у ползунка — границы): двойной клик по полю — ввод числа
		case ValueType::VECTOR2:
		{
			float* value = reinterpret_cast<float*>( property.dataPtr<XMFLOAT2>() );
			if( property.controlType() == GUIControlType::RANGE )
			{
				// Полоса: x — начало, y — конец; ImGui не даёт им перейти друг через друга
				const std::string format = numberFormat( property, value[1] );
				ImGui::DragFloatRange2( label, &value[0], &value[1], vectorSpeed( property, value[1] ), property.low(), property.high(),
										format.c_str(), nullptr, flags | ImGuiSliderFlags_AlwaysClamp );
			}
			else
				ImGui::DragFloat2( label, value, vectorSpeed( property, value[0] ), property.low(), property.high(), "%.3f", flags );
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
					ImGui::DragFloat3( label, value, vectorSpeed( property, value[0] ), property.low(), property.high(), "%.3f", flags );
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
					ImGui::DragFloat4( label, value, vectorSpeed( property, value[0] ), property.low(), property.high(), "%.3f", flags );
					break;
			}
			break;
		}
		case ValueType::INT:
		{
			NumberOptions options;
			options.low = property.low();
			options.high = property.high();
			options.fill = property.controlType() != GUIControlType::DRAG;
			options.speed = property.controlType() == GUIControlType::DRAG ? 0.1f : 0.0f;
			options.step = property.step();
			numberField( label, property.dataPtr<int32_t>(), options );
			break;
		}
		case ValueType::UINT:
			// Выбор текстуры по id пока не сделан
			ImGui::Text( "%u", property.data<uint32_t>() );
			break;
	}
	ImGui::EndGroup();
	// Правка — пока контрол (поле, стрелка, флажок, цвет) активен: одна запись истории. По состоянию, а не по событиям
	// ImGui: после клика по полю оно на кадр неактивно, пока включается ввод числа, — пустая правка отбрасывается, а ввод
	// начинает свою
	const bool wasEditing = property.editing();
	const bool active = ImGui::IsItemActive();
	markEditing( property, active );
	if( editHistory )
	{
		if( active && !wasEditing )
			editHistory->begin( owner + ": " + name, { &property } );
		else if( !active && wasEditing && editHistory->editing() )
			editHistory->end();
	}
}

void resetCell( const std::string& name, Property& property )
{
	ImGui::TableNextColumn();
	if( !property.modified() )
		return;
	ImGui::PushID( name.c_str() );
	if( ImGui::SmallButton( "reset" ) )
	{
		if( editHistory )
			editHistory->begin( "Reset " + name, { &property } );
		property.resetToSaved();
		if( editHistory )
			editHistory->end();
	}
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
		controlCell( name, property, container.name() );
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

// Кнопки окна — строкой под его свойствами, с переносом по ширине панели
void drawButtons( PropertyContainer& container, const std::string& filter, bool containerMatches )
{
	bool first = true;
	for( const PropertyContainer::Button& button : container.buttons() )
	{
		if( !containerMatches && !matches( button.name, filter ) )
			continue;
		const float width = ImGui::CalcTextSize( button.name.c_str() ).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		if( !first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x )
			ImGui::SameLine();
		first = false;
		if( ImGui::Button( button.name.c_str() ) )
		{
			pressedContainer = &container;
			pressedButton = button.name;
		}
		if( !button.tooltip.empty() )
			ImGui::SetItemTooltip( "%s", button.tooltip.c_str() );
	}
}

void drawContainerImpl( PropertyContainer& container, const std::string& filter, bool parentMatches, PropertyContainer* focus )
{
	const bool containerMatches = parentMatches || ( !filter.empty() && matches( container.name(), filter ) );
	drawProperties( container, filter, containerMatches );
	drawButtons( container, filter, containerMatches );
	for( PropertyContainer* sub : container.subContainer() )
	{
		if( !containerMatches && !hasMatches( *sub, filter ) )
			continue;
		ImGui::PushID( sub );
		// «*» — изменено; ID заголовка — постоянный («###»), иначе первая же правка внутри меняла его и сворачивала подокно
		const std::string title = ( sub->modified() ? sub->name() + "  *" : sub->name() ) + "###header";
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
	for( const PropertyContainer::Button& button : container.buttons() )
	{
		if( matches( button.name, filter ) )
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

bool takePressedButton( PropertyContainer*& container, std::string& button )
{
	if( !pressedContainer )
		return false;
	container = pressedContainer;
	button = std::move( pressedButton );
	pressedContainer = nullptr;
	pressedButton.clear();
	return true;
}

bool numberField( const char* id, float* value, const char* format, const NumberOptions& options )
{
	return numberFieldImpl( id, value, ImGuiDataType_Float, format, options );
}

bool numberField( const char* id, int32_t* value, const NumberOptions& options )
{
	return numberFieldImpl( id, value, ImGuiDataType_S32, "%d", options );
}

void endFrame()
{
	if( editedProperty && !editedPropertyDrawn )
	{
		editedProperty->setEditing( false );
		editedProperty = nullptr;
		if( editHistory && editHistory->editing() )
			editHistory->end();
	}
	editedPropertyDrawn = false;
}

void setHistory( Editor::EditHistory* history )
{
	editHistory = history;
}

Editor::EditHistory* history()
{
	return editHistory;
}

}
