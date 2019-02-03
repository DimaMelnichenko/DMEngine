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
	//ImGui::StyleColorsLight();
	//ImGui::StyleColorsDark();
	ImGui::StyleColorsClassic();
	m_isInited = true;
}

void GUI::Begin()
{
	//static bool show_demo_window = true;
	//static bool show_another_window = false;

	DMD3D::instance().GetDeviceContext()->VSSetShader( nullptr, nullptr, 0 );
	DMD3D::instance().GetDeviceContext()->GSSetShader( nullptr, nullptr, 0 );
	DMD3D::instance().GetDeviceContext()->HSSetShader( nullptr, nullptr, 0 );
	DMD3D::instance().GetDeviceContext()->DSSetShader( nullptr, nullptr, 0 );
	DMD3D::instance().GetDeviceContext()->PSSetShader( nullptr, nullptr, 0 );

	static ImVec4 clear_color = ImVec4( 0.45f, 0.55f, 0.60f, 1.00f );

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();


	Frame();
}

void GUI::End()
{
	// Rendering
	ImGui::Render();

	ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
	clearAfterRender();
}

void GUI::Frame()
{
	

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
	//renderSceneObject();


	showPropertiesTree();

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
/*
void GUI::renderSceneObject()
{
	struct ModelParam
	{
		XMFLOAT3 pos;
		XMFLOAT3 scale;

		void posReset()
		{
			pos.x = 0;
			pos.y = 0;
			pos.z = 0;
		}
		void scaleReset()
		{
			scale.x = 1.0;
			scale.y = 1.0;
			scale.z = 1.0;
		}
	};

	static std::unordered_map<uint32_t, ModelParam> modelsPos;
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
				modelsPos[model->id()].pos = pair.second->transformBuffer().posf3();
				ImGui::DragFloat3( "Position:", (float*)&modelsPos[model->id()].pos, 0.01, -10000.0, 10000.0 );
				if( ImGui::Button( "reset pos" ) )
				{
					modelsPos[model->id()].posReset();
				}
				pair.second->transformBuffer().setPosition( modelsPos[model->id()].pos );

				modelsPos[model->id()].scale = pair.second->transformBuffer().scale();
				ImGui::DragFloat3( "Scale:", (float*)&modelsPos[model->id()].scale, 0.001, 0.0, 10000.0 );
				if( ImGui::Button( "reset scale" ) )
				{
					modelsPos[model->id()].scaleReset();
				}
				pair.second->transformBuffer().setScale( modelsPos[model->id()].scale );

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
								case ParameterType::float4:
								{
									//XMFLOAT4 vec4 = pair.second.vector();
									ImGui::ColorEdit4( label.data(), (float*)pair.second.vectorPtr() );
									break;
								}
								case ParameterType::textureId:
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

*/


void GUI::printCamera( DMCamera& camera )
{
	ImGui::Begin( "Camera", nullptr, ImVec2( 256, 300 ), 1.0 );

	ImGui::Text( "Position:\nx:%f\ny:%f\nz:%f", camera.position().x, camera.position().y, camera.position().z );

	parsePropertiesAndCreateControls( &camera.m_properties );

	ImGui::End();
}

void GUI::parsePropertiesAndCreateControls( PropertyContainer* propertyContainer )
{	
	for( auto& [name,property] : propertyContainer->propertyMap() )
	{
		switch( property.valueType() )
		{
			case ValueType::BOOL:
			{
				ImGui::Checkbox( name.data(), property.dataPtr<bool>() );
				break;
			}
			case ValueType::FLOAT:
			{
				switch( property.controlType() )
				{
					case GUIControlType::SLIDER:
						ImGui::SliderFloat( name.data(), property.dataPtr<float>(), property.low(), property.high() );
						break;
					case GUIControlType::DRAG:
						ImGui::DragFloat( name.data(), property.dataPtr<float>(), 1.0f, property.low(), property.high() );
						break;
				}
				break;
			}
			case ValueType::VECTOR2:
			{
				XMFLOAT2* value = property.dataPtr<XMFLOAT2>();
				switch( property.controlType() )
				{	
					case GUIControlType::SLIDER:
						ImGui::SliderFloat2( name.data(), property.dataPtr<float>(), property.low(), property.high() );
						break;
					case GUIControlType::DRAG:
						ImGui::DragFloat2( name.data(), property.dataPtr<float>(), 0.1f, property.low(), property.high() );
						break;
				}
				break;
			}
			case ValueType::VECTOR3:
			{
				XMFLOAT3* value = property.dataPtr<XMFLOAT3>();
				switch( property.controlType() )
				{
					case GUIControlType::COLOR:
						ImGui::ColorEdit3( name.data(), (float*)value );
					case GUIControlType::SLIDER:
						ImGui::SliderFloat3( name.data(), (float*)value, property.low(), property.high() );
						break;
					case GUIControlType::DRAG:
						ImGui::DragFloat3( name.data(), (float*)value, 0.1f, property.low(), property.high() );
						break;
				}
				break;
			}
			case ValueType::VECTOR4:
			{
				XMFLOAT4* value = property.dataPtr<XMFLOAT4>();
				switch( property.controlType() )
				{
					case GUIControlType::COLOR:
						ImGui::ColorEdit4( name.data(), (float*)value );
						break;
					case GUIControlType::SLIDER:
						ImGui::SliderFloat4( name.data(), (float*)value, property.low(), property.high() );
						break;
					case GUIControlType::DRAG:
						ImGui::DragFloat4( name.data(), (float*)value, 0.1f, property.low(), property.high() );
						break;
				}
				break;
			}
			case ValueType::INT:
			{
				int32_t* value = property.dataPtr<int32_t>();
				switch( property.controlType() )
				{
					case GUIControlType::SLIDER:
						ImGui::SliderInt( name.data(), property.dataPtr<int32_t>(), property.low(), property.high() );
						break;
					case GUIControlType::DRAG:
						ImGui::DragInt( name.data(), property.dataPtr<int32_t>(), 1.0f, property.low(), property.high() );
						break;
				}
				break;
			}
			case ValueType::UINT:
			{
				uint32_t* value = property.dataPtr<uint32_t>();
				ImGui::BeginCombo( "Texture select", "select index" );
				//ImGui::Combo()
				ImGui::EndCombo();
				break;
			}
		}
	}
}

void GUI::addPropertyWatching( PropertyContainer* propertyContainer )
{
	m_propertiesMap.insert_or_assign( propertyContainer->name(), propertyContainer );
}

void GUI::parsePropertiesTree( PropertyContainer* propertyContainer )
{
	if( ImGui::TreeNode( (void*)(intptr_t)propertyContainer->name().data(), propertyContainer->name().data() ) )
	{
		parsePropertiesAndCreateControls( propertyContainer );
		for( auto subConatiner : propertyContainer->subContainer() )
		{
			parsePropertiesTree( subConatiner );
		}
		ImGui::TreePop();
	}
}

void GUI::showPropertiesTree()
{
	ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	ImGui::Begin( "Scene Objects", nullptr, ImVec2( 256, 300 ), 1.0 );
	int counter = 0;
	if( ImGui::TreeNode( "Properties Objects Tree" ) )
	{
		for( auto[name,propertyContainer] : m_propertiesMap )
		{
			parsePropertiesTree( propertyContainer );
		}
		ImGui::TreePop();
	}
	ImGui::End();
}