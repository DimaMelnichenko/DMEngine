#include "GUI.h"
#include <algorithm>
#include <cmath>
#include "imgui.h"
#ifdef IMGUI_HAS_DOCK
#include "imgui_internal.h"
#endif
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"
#include "D3D\DMD3D.h"
#include "Camera\DMCamera.h"

namespace
{

// Имена окон — они же ключи раскладки в imgui_editor.ini
const char* const outlinerWindow = "Outliner";
const char* const detailsWindow = "Details";
const char* const outputWindow = "Output";
const char* const statsWindow = "Stats";
const char* const cameraWindow = "Camera";

// Доли экрана раскладки по умолчанию
constexpr float leftShare = 0.17f;
constexpr float rightShare = 0.25f;
constexpr float bottomShare = 0.26f;

}

GUI::GUI()
{
}

GUI::~GUI()
{
	if( m_isInited )
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
}

void GUI::Initialize( HWND hwnd )
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	// Раскладка окон — своя, не в git (строится кодом, Window > Reset layout); прежний imgui.ini больше не читается
	io.IniFilename = "imgui_editor.ini";
#ifdef IMGUI_HAS_DOCK
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif

	ImGui_ImplWin32_Init( hwnd );
	// Бэкенд DX12: шрифт и картинки GUI — дескрипторы из общей shader-visible кучи движка (callback'и), кадров в полёте
	// и формат заднего буфера — как у DMD3D; рисует в текущий командный список кадра (GUI::End)
	DMD3D& d3d = DMD3D::instance();
	ImGui_ImplDX12_InitInfo info;
	info.Device = d3d.device();
	info.CommandQueue = d3d.directQueue();
	info.NumFramesInFlight = DMD3D::frameCount;
	info.RTVFormat = DMD3D::backBufferViewFormat;
	info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	info.SrvDescriptorHeap = d3d.shaderVisibleHeap();
	info.SrvDescriptorAllocFn = []( ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu )
	{
		const Descriptor descriptor = DMD3D::instance().allocateShaderDescriptor();
		*cpu = descriptor.cpu;
		*gpu = descriptor.gpu;
	};
	info.SrvDescriptorFreeFn = []( ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE )
	{
		DMD3D::instance().freeShaderDescriptor( cpu );
	};
	ImGui_ImplDX12_Init( &info );

	applyStyle( hwnd );
	m_isInited = true;
}

void GUI::applyStyle( HWND hwnd )
{
	// Тёмная тема в духе редакторов: приглушённые панели, акцент — синий, мягкие скругления
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;
	style.ChildRounding = 3.0f;
	style.FrameRounding = 3.0f;
	style.PopupRounding = 3.0f;
	style.GrabRounding = 3.0f;
	style.TabRounding = 3.0f;
	style.WindowPadding = ImVec2( 8.0f, 8.0f );
	style.FramePadding = ImVec2( 6.0f, 3.0f );
	style.ItemSpacing = ImVec2( 6.0f, 4.0f );
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	ImVec4* colors = style.Colors;
	colors[ImGuiCol_WindowBg] = ImVec4( 0.11f, 0.11f, 0.12f, 0.96f );
	colors[ImGuiCol_ChildBg] = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f );
	colors[ImGuiCol_PopupBg] = ImVec4( 0.09f, 0.09f, 0.10f, 0.98f );
	colors[ImGuiCol_MenuBarBg] = ImVec4( 0.08f, 0.08f, 0.09f, 1.0f );
	colors[ImGuiCol_FrameBg] = ImVec4( 0.18f, 0.18f, 0.20f, 1.0f );
	colors[ImGuiCol_FrameBgHovered] = ImVec4( 0.24f, 0.25f, 0.28f, 1.0f );
	colors[ImGuiCol_FrameBgActive] = ImVec4( 0.28f, 0.30f, 0.34f, 1.0f );
	colors[ImGuiCol_TitleBg] = ImVec4( 0.08f, 0.08f, 0.09f, 1.0f );
	colors[ImGuiCol_TitleBgActive] = ImVec4( 0.12f, 0.13f, 0.15f, 1.0f );
	colors[ImGuiCol_Header] = ImVec4( 0.20f, 0.22f, 0.26f, 1.0f );
	colors[ImGuiCol_HeaderHovered] = ImVec4( 0.26f, 0.30f, 0.38f, 1.0f );
	colors[ImGuiCol_HeaderActive] = ImVec4( 0.26f, 0.42f, 0.66f, 1.0f );
	colors[ImGuiCol_Button] = ImVec4( 0.22f, 0.24f, 0.28f, 1.0f );
	colors[ImGuiCol_ButtonHovered] = ImVec4( 0.28f, 0.40f, 0.60f, 1.0f );
	colors[ImGuiCol_ButtonActive] = ImVec4( 0.26f, 0.46f, 0.74f, 1.0f );
	colors[ImGuiCol_SliderGrab] = ImVec4( 0.36f, 0.55f, 0.85f, 1.0f );
	colors[ImGuiCol_SliderGrabActive] = ImVec4( 0.45f, 0.65f, 0.95f, 1.0f );
	colors[ImGuiCol_CheckMark] = ImVec4( 0.45f, 0.68f, 1.0f, 1.0f );
	colors[ImGuiCol_Tab] = ImVec4( 0.14f, 0.15f, 0.17f, 1.0f );
	colors[ImGuiCol_TabHovered] = ImVec4( 0.28f, 0.40f, 0.60f, 1.0f );
	colors[ImGuiCol_TableRowBgAlt] = ImVec4( 1.0f, 1.0f, 1.0f, 0.03f );
	// Задний буфер пишется через sRGB-вид (DMD3D::backBufferViewFormat): цвета темы заданы в sRGB, иначе тёмные панели
	// после преобразования вышли бы светло-серыми
	for( int i = 0; i < ImGuiCol_COUNT; ++i )
	{
		for( float* channel : { &colors[i].x, &colors[i].y, &colors[i].z } )
			*channel = *channel <= 0.04045f ? *channel / 12.92f : std::pow( ( *channel + 0.055f ) / 1.055f, 2.4f );
	}

	// Масштаб по DPI монитора (процесс объявляет поддержку DPI): на экране 150 % — всё в полтора раза крупнее
	const float scale = std::max( static_cast<float>( GetDpiForWindow( hwnd ) ) / 96.0f, 1.0f );
	style.ScaleAllSizes( scale );
	style.FontScaleDpi = scale;
	// Шрифт Windows (с кириллицей) вместо растрового по умолчанию; нет его — встроенный
	ImGuiIO& io = ImGui::GetIO();
	if( GetFileAttributesA( "C:\\Windows\\Fonts\\segoeui.ttf" ) != INVALID_FILE_ATTRIBUTES )
		io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\segoeui.ttf", 15.0f );
}

void GUI::Begin( const GS::FrameStats& stats, DMCamera& camera )
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Ctrl+S — первое действие (сохранение уровня), ` — консоль
	if( ImGui::Shortcut( ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal ) && !m_actions.empty() )
		m_actions.front().run();
	if( ImGui::Shortcut( ImGuiKey_GraveAccent, ImGuiInputFlags_RouteGlobal ) )
	{
		m_showOutput = true;
		m_console.focusInput();
	}

	drawMenu();
	float sceneX = 0.0f;
	float sceneY = 0.0f;
	layout( sceneX, sceneY );

	m_stats.update( stats );
	if( m_showOverlay )
		m_stats.drawOverlay( sceneX, sceneY );
	if( m_showOutliner && placeNext( outlinerWindow ) )
		m_outliner.draw( m_entries, m_selected, &m_showOutliner );
	if( m_showDetails && placeNext( detailsWindow ) )
	{
		m_details.draw( m_selected >= 0 && m_selected < static_cast<int>( m_entries.size() ) ? &m_entries[m_selected] : nullptr,
						&m_showDetails, m_focus );
		m_focus = nullptr;
	}
	if( m_showOutput && placeNext( outputWindow ) )
		m_console.draw( &m_showOutput, m_commandNames, m_containers );
	if( m_showStats && placeNext( statsWindow ) )
		m_stats.draw( &m_showStats );
	if( m_showCamera && placeNext( cameraWindow ) )
		m_camera.draw( camera, &m_showCamera );
	if( m_showTextures )
		m_textures.draw( &m_showTextures );
	if( m_showHotkeys )
		drawHotkeys();
	drawNotification();
	m_resetLayout = false;
	m_firstFrame = false;
}

void GUI::End()
{
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData( ImGui::GetDrawData(), DMD3D::instance().commandList() );
}

void GUI::drawMenu()
{
	if( !ImGui::BeginMainMenuBar() )
		return;
	if( ImGui::BeginMenu( "File" ) )
	{
		for( const Editor::Action& action : m_actions )
		{
			if( ImGui::MenuItem( action.name.c_str(), action.shortcut.empty() ? nullptr : action.shortcut.c_str() ) )
				action.run();
		}
		ImGui::EndMenu();
	}
	if( ImGui::BeginMenu( "View" ) )
	{
		for( const Editor::Toggle& toggle : m_toggles )
		{
			if( toggle.debugView )
				continue;
			if( ImGui::MenuItem( toggle.name.c_str(), toggle.shortcut.empty() ? nullptr : toggle.shortcut.c_str(), toggle.state() ) )
				toggle.toggle();
		}
		if( ImGui::BeginMenu( "Debug views" ) )
		{
			for( const Editor::Toggle& toggle : m_toggles )
			{
				if( toggle.debugView && ImGui::MenuItem( toggle.name.c_str(), toggle.shortcut.empty() ? nullptr : toggle.shortcut.c_str(), toggle.state() ) )
					toggle.toggle();
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		ImGui::MenuItem( "Frame overlay", nullptr, &m_showOverlay );
		ImGui::EndMenu();
	}
	if( ImGui::BeginMenu( "Window" ) )
	{
		ImGui::MenuItem( outlinerWindow, nullptr, &m_showOutliner );
		ImGui::MenuItem( detailsWindow, nullptr, &m_showDetails );
		ImGui::MenuItem( "Output (console, log)", "`", &m_showOutput );
		ImGui::MenuItem( statsWindow, nullptr, &m_showStats );
		ImGui::MenuItem( cameraWindow, nullptr, &m_showCamera );
		ImGui::MenuItem( "Texture Library", nullptr, &m_showTextures );
		ImGui::Separator();
		if( ImGui::MenuItem( "Reset layout" ) )
		{
			m_resetLayout = true;
			m_showOutliner = m_showDetails = m_showOutput = m_showStats = m_showCamera = m_showOverlay = true;
		}
		ImGui::EndMenu();
	}
	if( ImGui::BeginMenu( "Help" ) )
	{
		ImGui::MenuItem( "Hotkeys", nullptr, &m_showHotkeys );
		ImGui::EndMenu();
	}

	// Справа: несохранённые правки уровня
	if( modified() )
	{
		const char* text = "Level changed — Ctrl+S to save";
		ImGui::SameLine( ImGui::GetWindowWidth() - ImGui::CalcTextSize( text ).x - ImGui::GetStyle().ItemSpacing.x * 2.0f );
		ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.3f, 1.0f ), "%s", text );
	}
	ImGui::EndMainMenuBar();
}

void GUI::placeWindow( const char* name, float x, float y, float width, float height )
{
	m_places.push_back( { name, ImVec2( x, y ), ImVec2( width, height ) } );
}

bool GUI::placeNext( const char* name )
{
	// Место окна по умолчанию (без докинга): при первом появлении и по Reset layout
	for( const Place& place : m_places )
	{
		if( std::string( place.name ) == name )
		{
			const ImGuiCond condition = m_resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
			ImGui::SetNextWindowPos( place.position, condition );
			ImGui::SetNextWindowSize( place.size, condition );
		}
	}
	return true;
}

void GUI::layout( float& sceneX, float& sceneY )
{
	// Под главным меню: в первом кадре рабочая область окна ещё не знает его высоты
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float menu = ImGui::GetFrameHeight();
	const ImVec2 origin( viewport->Pos.x, viewport->Pos.y + menu );
	const ImVec2 size( viewport->Size.x, viewport->Size.y - menu );
	const float left = std::max( size.x * leftShare, 260.0f );
	const float right = std::max( size.x * rightShare, 380.0f );
	const float bottom = std::max( size.y * bottomShare, 180.0f );
	m_places.clear();

#ifdef IMGUI_HAS_DOCK
	// Dockspace на весь экран под меню; центральный узел прозрачен — там сцена
	const ImGuiID dockspace = ImGui::DockSpaceOverViewport( 0, viewport, ImGuiDockNodeFlags_PassthruCentralNode );
	const bool empty = ImGui::DockBuilderGetNode( dockspace ) == nullptr || ImGui::DockBuilderGetNode( dockspace )->IsLeafNode();
	if( m_resetLayout || ( m_firstFrame && empty ) )
	{
		ImGui::DockBuilderRemoveNode( dockspace );
		ImGui::DockBuilderAddNode( dockspace, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode );
		ImGui::DockBuilderSetNodeSize( dockspace, size );
		ImGuiID center = dockspace;
		const ImGuiID leftNode = ImGui::DockBuilderSplitNode( center, ImGuiDir_Left, leftShare, nullptr, &center );
		const ImGuiID rightNode = ImGui::DockBuilderSplitNode( center, ImGuiDir_Right, rightShare / ( 1.0f - leftShare ), nullptr, &center );
		const ImGuiID bottomNode = ImGui::DockBuilderSplitNode( center, ImGuiDir_Down, bottomShare, nullptr, &center );
		ImGuiID leftTop = leftNode;
		const ImGuiID leftBottom = ImGui::DockBuilderSplitNode( leftTop, ImGuiDir_Down, 0.45f, nullptr, &leftTop );
		ImGui::DockBuilderDockWindow( outlinerWindow, leftTop );
		ImGui::DockBuilderDockWindow( cameraWindow, leftBottom );
		ImGui::DockBuilderDockWindow( statsWindow, leftBottom );
		ImGui::DockBuilderDockWindow( detailsWindow, rightNode );
		ImGui::DockBuilderDockWindow( outputWindow, bottomNode );
		ImGui::DockBuilderFinish( dockspace );
	}
	if( const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode( dockspace ) )
	{
		sceneX = central->Pos.x;
		sceneY = central->Pos.y;
		return;
	}
#else
	// Без докинга — места окон: слева Outliner, Camera и Stats, справа Details, снизу между ними Output
	const float height = size.y;
	placeWindow( outlinerWindow, origin.x, origin.y, left, height * 0.45f );
	placeWindow( cameraWindow, origin.x, origin.y + height * 0.45f, left, height * 0.2f );
	placeWindow( statsWindow, origin.x, origin.y + height * 0.65f, left, height * 0.35f );
	placeWindow( detailsWindow, origin.x + size.x - right, origin.y, right, height );
	placeWindow( outputWindow, origin.x + left, origin.y + height - bottom, size.x - left - right, bottom );
#endif
	sceneX = origin.x + left;
	sceneY = origin.y;
}

void GUI::drawNotification()
{
	const double age = ImGui::GetTime() - m_noticeTime;
	if( age > 4.0 || m_notice.empty() )
		return;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float padding = 16.0f;
	ImGui::SetNextWindowPos( ImVec2( viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + padding ), ImGuiCond_Always,
							 ImVec2( 0.5f, 0.0f ) );
	ImGui::SetNextWindowBgAlpha( 0.85f * static_cast<float>( std::min( 1.0, ( 4.0 - age ) * 2.0 ) ) );
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
							 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
#ifdef IMGUI_HAS_DOCK
	flags |= ImGuiWindowFlags_NoDocking;
#endif
	if( ImGui::Begin( "##notice", nullptr, flags ) )
	{
		if( m_noticeError )
			ImGui::TextColored( ImVec4( 1.0f, 0.45f, 0.4f, 1.0f ), "%s", m_notice.c_str() );
		else
			ImGui::TextUnformatted( m_notice.c_str() );
	}
	ImGui::End();
}

void GUI::drawHotkeys()
{
	ImGui::SetNextWindowSize( ImVec2( 420.0f, 0.0f ), ImGuiCond_FirstUseEver );
	if( ImGui::Begin( "Hotkeys", &m_showHotkeys, ImGuiWindowFlags_AlwaysAutoResize ) )
	{
		static const char* const keys[][2] = {
			{ "Right mouse button + mouse", "look around (camera)" },
			{ "W A S D, Space / C", "move the camera" },
			{ "I", "fly mode: always look with the mouse" },
			{ "G", "game view: hide the editor" },
			{ "Q", "wireframe" },
			{ "1", "terrain on / off" },
			{ "3 / 4", "scatter compute / drawing" },
			{ "P", "screenshot with the editor (screenshotN.jpg)" },
			{ "Ctrl+S", "save level environment" },
			{ "`", "console" },
			{ "Ctrl + click on a slider", "type a value" },
			{ "Esc", "exit" } };
		if( ImGui::BeginTable( "keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit ) )
		{
			for( const auto& key : keys )
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( key[0] );
				ImGui::TableNextColumn();
				ImGui::TextDisabled( "%s", key[1] );
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
}

void GUI::addPropertyWatching( PropertyContainer* propertyContainer, Editor::Category category, GS::SceneObject* object, bool saved )
{
	// В порядке регистрации; одинаковые имена окон друг друга не затирают
	if( !propertyContainer || std::find( m_containers.begin(), m_containers.end(), propertyContainer ) != m_containers.end() )
		return;
	propertyContainer->markSaved();
	m_containers.push_back( propertyContainer );
	m_entries.push_back( { propertyContainer, category, object, saved } );
}

void GUI::addAction( const std::string& name, const std::string& shortcut, std::function<void()> run )
{
	m_actions.push_back( { name, shortcut, std::move( run ) } );
}

bool GUI::select( const std::string& name )
{
	for( int i = 0; i < static_cast<int>( m_entries.size() ); ++i )
	{
		if( m_entries[i].properties->name() == name )
		{
			m_selected = i;
			m_showDetails = true;
			return true;
		}
	}
	return false;
}

bool GUI::runAction( const std::string& name )
{
	for( const Editor::Action& action : m_actions )
	{
		if( action.name == name )
		{
			action.run();
			return true;
		}
	}
	return false;
}

void GUI::addToggle( Editor::Toggle toggle )
{
	m_toggles.push_back( std::move( toggle ) );
}

namespace
{

bool containsProperties( PropertyContainer& container, const PropertyContainer* target )
{
	if( &container == target )
		return true;
	for( PropertyContainer* sub : container.subContainer() )
	{
		if( containsProperties( *sub, target ) )
			return true;
	}
	return false;
}

}

bool GUI::focusProperties( PropertyContainer* container )
{
	for( int i = 0; i < static_cast<int>( m_entries.size() ); ++i )
	{
		if( containsProperties( *m_entries[i].properties, container ) )
		{
			m_selected = i;
			m_focus = container;
			m_showDetails = true;
			return true;
		}
	}
	return false;
}

void GUI::markSaved()
{
	for( Editor::Entry& entry : m_entries )
	{
		if( entry.saved )
			entry.properties->markSaved();
	}
}

bool GUI::modified() const
{
	for( const Editor::Entry& entry : m_entries )
	{
		if( entry.saved && entry.properties->modified() )
			return true;
	}
	return false;
}

void GUI::notify( const std::string& text, bool error )
{
	m_notice = text;
	m_noticeError = error;
	m_noticeTime = m_isInited ? ImGui::GetTime() : 0.0;
}

bool GUI::wantsMouse() const
{
	return m_isInited && ImGui::GetIO().WantCaptureMouse;
}

bool GUI::wantsKeyboard() const
{
	return m_isInited && ImGui::GetIO().WantTextInput;
}

void GUI::setMouseEnabled( bool enabled )
{
	if( !m_isInited )
		return;
	ImGuiIO& io = ImGui::GetIO();
	if( enabled )
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
	else
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
}
