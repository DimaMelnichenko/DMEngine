#include "GUI.h"
#include <algorithm>
#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"
#include "D3D\DMD3D.h"
#include "System.h"


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
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	ImGui_ImplWin32_Init( hwnd );
	// Бэкенд DX12: шрифт и картинки GUI — дескрипторы из общей shader-visible кучи движка (callback'и), кадров в полёте
	// и формат заднего буфера — как у DMD3D; рисует в текущий командный список кадра (GUI::End)
	DMD3D& d3d = DMD3D::instance();
	ImGui_ImplDX12_InitInfo info;
	info.Device = d3d.GetDevice();
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

	// Setup style
	//ImGui::StyleColorsLight();
	//ImGui::StyleColorsDark();
	ImGui::StyleColorsClassic();
	m_isInited = true;
}

void GUI::Begin()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();


	Frame();
}

void GUI::End()
{
	// Rendering
	ImGui::Render();

	ImGui_ImplDX12_RenderDrawData( ImGui::GetDrawData(), DMD3D::instance().commandList() );
	clearAfterRender();
}

void GUI::Frame()
{
	

	// 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
	{
		ImGui::Begin( "Statistic" );                          // Create a window called "Hello, world!" and append into it.

		ImGui::Text( "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate );

		for( auto& pair : m_counterInfoList )
		{
			ImGui::Text( pair.first.data(), pair.second );
		}

		

		ImGui::End();
	}
	renderTextureLibrary();
	//renderSceneObject();


	showPropertiesTree();

}

void GUI::addCounterInfo( const std::string& text, float value )
{
	m_counterInfoList.emplace_back( text, value );
}

void GUI::skipFrame()
{
	clearAfterRender();
}

void GUI::clearAfterRender()
{
	m_counterInfoList.clear();
}

void GUI::renderTextureLibrary()
{
	ImGui::SetNextWindowSize( ImVec2( 256, 300 ), ImGuiCond_FirstUseEver );
	ImGui::Begin( "Texture Library" );
	ImGui::BeginChild( "Scrolling" );
	for( auto& item : GS::System::textures() )
	{
		// ImTextureID у бэкенда imgui_impl_dx12 — GPU-дескриптор вида из общей кучи
		if( item.second->srv().valid() )
			ImGui::Image( static_cast<ImTextureID>( item.second->srv().descriptor().gpu.ptr ), ImVec2( 256, 256 ) );
		ImGui::Text( "id:%d name:%s", item.first, item.second->name().data() );
	}
	ImGui::EndChild();
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
	ImGui::SetNextWindowSize( ImVec2( 256, 300 ), ImGuiCond_FirstUseEver );
	ImGui::Begin( "Scene Objects" );
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
	ImGui::SetNextWindowSize( ImVec2( 256, 300 ), ImGuiCond_FirstUseEver );
	ImGui::Begin( "Camera" );

	ImGui::Text( "Position:\nx:%f\ny:%f\nz:%f", camera.position().x, camera.position().y, camera.position().z );

	parsePropertiesAndCreateControls( &camera.m_properties );

	ImGui::End();
}

void GUI::parsePropertiesAndCreateControls( PropertyContainer* propertyContainer )
{	
	for( const std::string& name : propertyContainer->names() )
	{
		Property& property = propertyContainer->property( name );
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
					case GUIControlType::LABEL:
						ImGui::LabelText( name.data(), "%.4g", property.data<float>() );
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
						ImGui::SliderFloat2( name.data(), (float*)value, property.low(), property.high() );
						break;
					case GUIControlType::DRAG:
						ImGui::DragFloat2( name.data(), (float*)value, 0.1f, property.low(), property.high() );
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
						break;
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
				// Выбор текстуры по id пока не сделан; EndCombo — только после открытого BeginCombo
				if( ImGui::BeginCombo( name.data(), std::to_string( property.data<uint32_t>() ).c_str() ) )
					ImGui::EndCombo();
				break;
			}
		}
	}
}

void GUI::addPropertyWatching( PropertyContainer* propertyContainer )
{
	// В порядке регистрации; одинаковые имена окон друг друга не затирают
	if( std::find( m_properties.begin(), m_properties.end(), propertyContainer ) == m_properties.end() )
		m_properties.push_back( propertyContainer );
}

void GUI::addAction( const std::string& label, std::function<void()> action )
{
	m_actions.emplace_back( label, std::move( action ) );
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
	// Шире прежних 256 пикселей: подписи свойств (Aerial perspective view distance scale) не обрезаются
	ImGui::SetNextWindowSize( ImVec2( 420, 300 ), ImGuiCond_FirstUseEver );
	ImGui::Begin( "Scene Objects" );
	for( const auto& [label, action] : m_actions )
	{
		if( ImGui::Button( label.c_str() ) )
			action();
	}
	int counter = 0;
	if( ImGui::TreeNode( "Properties Objects Tree" ) )
	{
		for( PropertyContainer* propertyContainer : m_properties )
		{
			parsePropertiesTree( propertyContainer );
		}
		ImGui::TreePop();
	}
	ImGui::End();
}