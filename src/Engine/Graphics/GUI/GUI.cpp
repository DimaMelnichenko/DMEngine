#include "GUI.h"
#include "imGUI\imgui.h"
#include "imGUI\imgui_impl_dx11.h"
#include "imGUI\imgui_impl_win32.h"
#include "D3D\DMD3D.h"


GUI::GUI()
{
}


GUI::~GUI()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void GUI::Initialize( HWND hwnd )
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

	ImGui_ImplWin32_Init( hwnd );
	ImGui_ImplDX11_Init( DMD3D::instance().GetDevice(), DMD3D::instance().GetDeviceContext() );

	// Setup style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();
}

void GUI::Frame()
{
	//static bool show_demo_window = true;
	//static bool show_another_window = false;
	static ImVec4 clear_color = ImVec4( 0.45f, 0.55f, 0.60f, 1.00f );

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
	//if( show_demo_window )
	//	ImGui::ShowDemoWindow( &show_demo_window );

	// 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
	{
		static float f = 0.0f;
		static int counter = 0;

		ImGui::Begin( "Statistic" );                          // Create a window called "Hello, world!" and append into it.

		//ImGui::Text( "This is some useful text." );               // Display some text (you can use a format strings too)
		//ImGui::Checkbox( "Demo Window", &show_demo_window );      // Edit bools storing our window open/close state
		//ImGui::Checkbox( "Another Window", &show_another_window );

		//ImGui::SliderFloat( "float", &f, 0.0f, 1.0f );            // Edit 1 float using a slider from 0.0f to 1.0f    
		//ImGui::ColorEdit3( "clear color", (float*)&clear_color ); // Edit 3 floats representing a color

		//if( ImGui::Button( "Button" ) )                            // Buttons return true when clicked (most widgets return true when edited/activated)
		//	counter++;
		//ImGui::SameLine();
		//ImGui::Text( "counter = %d", counter );

		ImGui::Text( "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate );

		for( auto& pair : m_counterInfoList )
		{
			ImGui::Text( pair.first.data(), pair.second );
		}

		ImGui::End();
	}

	grassParam();

	// 3. Show another simple window.
	/*if( show_another_window )
	{
		ImGui::Begin( "Another Window", &show_another_window );   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
		ImGui::Text( "Hello from another window!" );
		if( ImGui::Button( "Close Me" ) )
			show_another_window = false;
		ImGui::End();
	}
	*/

	// Rendering
	ImGui::Render();
}

void GUI::Render()
{
	ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
	clearAfterRender();
}

void GUI::addCounterInfo( const std::string& text, float value )
{
	m_counterInfoList.emplace_back( text, value );
}

void GUI::clearAfterRender()
{
	m_counterInfoList.clear();
}

void GUI::grassParam()
{
	ImGui::Begin( "Grass Parameters" );
	ImGui::ColorEdit4( "Tint Color", (float*)&colorGrass );
	ImGui::ColorEdit4( "Tint Ambient", (float*)&ambientGrass );
	ImGui::End();
}