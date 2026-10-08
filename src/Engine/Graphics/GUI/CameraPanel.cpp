#include "CameraPanel.h"
#include <cmath>
#include <cstdio>
#include "imgui.h"
#include "Camera\DMCamera.h"
#include "PropertyWidgets.h"

using namespace DirectX;

void CameraPanel::draw( DMCamera& camera, bool* open )
{
	if( !ImGui::Begin( "Camera", open ) )
	{
		ImGui::End();
		return;
	}
	// Всё, чтобы по скриншоту поставить камеру так же: строка — в формате команды camera удалённого управления и
	// параметра -Camera (x,y,z,pitch,yaw; pitch > 0 — взгляд вниз, yaw 0 — на +Z, 90 — на +X)
	const XMFLOAT3& position = camera.position();
	const XMFLOAT2 rotation = camera.rotation();
	const float yaw = std::fmod( std::fmod( rotation.y, 360.0f ) + 360.0f, 360.0f );
	XMFLOAT3 direction;
	camera.viewDirection( &direction );
	ImGui::Text( "Position   %.2f, %.2f, %.2f", position.x, position.y, position.z );
	ImGui::Text( "Direction  %.3f, %.3f, %.3f", direction.x, direction.y, direction.z );
	ImGui::Text( "Pitch %.1f, yaw %.1f (degrees)", rotation.x, yaw );
	char line[128];
	std::snprintf( line, sizeof( line ), "%.2f,%.2f,%.2f,%.1f,%.1f", position.x, position.y, position.z, rotation.x, yaw );
	ImGui::TextDisabled( "camera %s", line );
	if( ImGui::Button( "Copy x,y,z,pitch,yaw" ) )
		ImGui::SetClipboardText( line );
	ImGui::Separator();
	PropertyWidgets::drawContainer( camera.m_properties, {} );
	ImGui::End();
}
