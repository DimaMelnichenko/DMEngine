#include "GUI.h"
#include "imGUI\imgui.h"
#include "imGUI\imgui_impl_dx11.h"
#include "imGUI\imgui_impl_win32.h"
#include "D3D\DMD3D.h"
#include "System.h"


GUI::GUI()
{
}


GUI::~GUI()
{
	if( m_isInited )
	{
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
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
	m_isInited = true;
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
	renderTextureLibrary();
	//renderMaterialLibrary();
	renderSceneObject();

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

void GUI::renderTextureLibrary()
{
	//static bool isOpen = false;
	static float alphaOverride = 1.0;
	ImGui::Begin( "Texture Library", nullptr, ImVec2( 256, 300 ), alphaOverride );
	ImGui::BeginChild( "Scrolling" );
	for( auto& item : GS::System::textures() )
	{
		ImGui::Image( item.second->srv().get(), ImVec2( 256, 256 ) );
		ImGui::Text( "id:%d name:%s", item.first, item.second->name().data() );
	}
	ImGui::EndChild();
	ImGui::End();
}

void GUI::renderMaterialLibrary()
{
	ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	ImGui::Begin( "Material Library", nullptr, ImVec2( 256, 300 ), 1.0 );
	if( ImGui::TreeNode( "Material Tree" ) )
	{
		for( auto& pair : GS::System::materials() )
		{
			GS::Material* material = pair.second.get();
			ImGui::TreeNodeEx( (void*)(intptr_t)pair.first, node_flags, material->name().data() );
		}
		ImGui::TreePop();
	}
	ImGui::End();
}

void GUI::renderSceneObject()
{
	ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	ImGui::Begin( "Scene Objects", nullptr, ImVec2( 256, 300 ), 1.0 );
	int counter = 0;
	if( ImGui::TreeNode( "Scene Objects Tree" ) )
	{
		for( auto& pair : GS::System::models() )
		{
			GS::DMModel* model = pair.second.get();
			if( ImGui::TreeNode( (void*)(intptr_t)pair.first, model->name().data() ) )
			{
				for( int lodIdx = 0; lodIdx < model->lodCount(); ++lodIdx )
				{
					GS::DMModel::LodBlock* lodBlock = model->getLodById( lodIdx );
					GS::Material* material = GS::System::materials().get( lodBlock->material ).get();
					if( ImGui::TreeNode( (void*)(intptr_t)(material->id() + lodIdx), material->name().data() ) )
					{					
						for( auto& pair : lodBlock->params )
						{
							counter++;
							std::string label = std::to_string( counter ) + ":" + pair.first;
							//ImGui::Separator();
							switch( pair.second.valueType() )
							{	
								case GS::Parameter::ValueType::float4:
								{
									//XMFLOAT4 vec4 = pair.second.vector();
									ImGui::ColorEdit4( label.data(), (float*)pair.second.vectorPtr() );
									break;
								}
								case GS::Parameter::ValueType::textureId:
								{
									//int texId = pair.second.textId();
									//ImGui::SliderInt( label.data(), (int*)pair.second.textIdPtr(), 1, GS::System::textures().size() );
									if( ImGui::BeginCombo( label.data(), GS::System::textures().get( pair.second.textId() )->name().data() ) )
									{
										for( auto& texturesPair : GS::System::textures() )
											if( ImGui::Selectable( texturesPair.second->name().data(), texturesPair.first == pair.second.textId() ) )
												pair.second.setValue( texturesPair.first );
										ImGui::EndCombo();
									}
									break;
								}
							}
						}
						ImGui::TreePop();
					}
				}
				ImGui::TreePop();
			}
		}
		ImGui::TreePop();
	}
	ImGui::End();
}