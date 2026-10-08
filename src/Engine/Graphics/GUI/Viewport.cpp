#include "Viewport.h"
#include <DirectXCollision.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "GUI.h"
#include "Scene\Camera\DMCamera.h"
#include "Scene\Model\ModelInstances.h"

using namespace DirectX;

namespace
{

// Вид и проекция главной камеры одной матрицей
XMMATRIX viewProjection( const DMCamera& camera )
{
	XMMATRIX view;
	XMMATRIX projection;
	camera.viewMatrix( &view );
	camera.projectionMatrix( &projection );
	return view * projection;
}

// Курсор над окном редактора; над сценой (центр докинга пропускает ввод) окна под курсором нет
bool overPanels()
{
	return GImGui->HoveredWindow != nullptr;
}

}

bool Viewport::sceneClicked()
{
	const ImGuiIO& io = ImGui::GetIO();
	// Нажатие на гизмо — его перетаскивание, а не выбор
	if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		m_pressedOverScene = !overPanels() && !ImGuizmo::IsOver();
	if( !ImGui::IsMouseReleased( ImGuiMouseButton_Left ) || !m_pressedOverScene )
		return false;
	m_pressedOverScene = false;
	const float slop = io.MouseDragThreshold;
	return io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < slop * slop;
}

void Viewport::update( const DMCamera& camera, GS::ModelInstances& models, GUI& gui )
{
	if( m_selected >= static_cast<int>( models.instanceCount() ) )
		m_selected = -1;

	ImGuizmo::BeginFrame();
	if( sceneClicked() )
		pickAt( camera, models, gui, ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y );
	drawSelection( camera, models );
	if( m_selected >= 0 )
	{
		drawToolbar();
		manipulate( camera, models );
	}
}

void Viewport::drawToolbar()
{
	const ImGuiIO& io = ImGui::GetIO();
	if( !io.WantTextInput )
	{
		if( ImGui::IsKeyPressed( ImGuiKey_5, false ) )
			m_gizmo = Gizmo::move;
		if( ImGui::IsKeyPressed( ImGuiKey_6, false ) )
			m_gizmo = Gizmo::rotate;
		if( ImGui::IsKeyPressed( ImGuiKey_7, false ) )
			m_gizmo = Gizmo::scale;
	}

	// Над сценой по центру, под меню
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + ImGui::GetFrameHeight() * 2.0f ),
							 ImGuiCond_Always, ImVec2( 0.5f, 0.0f ) );
	ImGui::SetNextWindowBgAlpha( 0.7f );
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
								   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
								   ImGuiWindowFlags_NoMove;
	if( ImGui::Begin( "##gizmo toolbar", nullptr, flags ) )
	{
		auto mode = [this]( const char* label, Gizmo gizmo, const char* tooltip )
		{
			if( ImGui::RadioButton( label, m_gizmo == gizmo ) )
				m_gizmo = gizmo;
			ImGui::SetItemTooltip( "%s", tooltip );
			ImGui::SameLine();
		};
		mode( "Move", Gizmo::move, "Move the selected instance (5)" );
		mode( "Rotate", Gizmo::rotate, "Rotate the selected instance (6)" );
		mode( "Scale", Gizmo::scale, "Scale the selected instance (7)" );
		ImGui::Checkbox( "Local axes", &m_localAxes );
		ImGui::SetItemTooltip( "Axes of the instance instead of the world" );
	}
	ImGui::End();
}

void Viewport::manipulate( const DMCamera& camera, GS::ModelInstances& models )
{
	XMMATRIX view;
	XMMATRIX projection;
	camera.viewMatrix( &view );
	camera.projectionMatrix( &projection );
	// Матрицы DirectXMath (векторы-строки) в памяти — как ждёт ImGuizmo: перенос в элементах 12…14
	XMFLOAT4X4 viewData;
	XMFLOAT4X4 projectionData;
	XMFLOAT4X4 world;
	XMStoreFloat4x4( &viewData, view );
	XMStoreFloat4x4( &projectionData, projection );
	XMStoreFloat4x4( &world, models.instanceMatrix( m_selected ) );

	const ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetOrthographic( false );
	// Под окнами редактора, как рамка; над окнами гизмо мышь не берёт
	ImGuizmo::SetDrawlist( ImGui::GetBackgroundDrawList() );
	ImGuizmo::SetRect( 0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y );
	// Над панелями гизмо мышь не берёт. Не по io.WantCaptureMouse: гизмо под курсором само просит его на следующий кадр
	// (SetNextFrameWantCaptureMouse), и выключение по нему мигало бы через кадр
	ImGuizmo::Enable( ImGuizmo::IsUsing() || !overPanels() );
	const ImGuizmo::OPERATION operation = m_gizmo == Gizmo::move ? ImGuizmo::TRANSLATE :
										  m_gizmo == Gizmo::rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
	const ImGuizmo::MODE mode = m_localAxes || m_gizmo == Gizmo::scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
	if( ImGuizmo::Manipulate( &viewData._11, &projectionData._11, operation, mode, &world._11 ) )
		models.setInstanceMatrix( m_selected, XMLoadFloat4x4( &world ) );
}

int Viewport::pickAt( const DMCamera& camera, GS::ModelInstances& models, GUI& gui, float x, float y )
{
	// Луч через точку: на ближней (z = 1, глубина обратная) и дальней (z = 0) плоскостях
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	const float ndcX = 2.0f * x / size.x - 1.0f;
	const float ndcY = 1.0f - 2.0f * y / size.y;
	const XMMATRIX inverse = XMMatrixInverse( nullptr, viewProjection( camera ) );
	const XMVECTOR nearPoint = XMVector3TransformCoord( XMVectorSet( ndcX, ndcY, 1.0f, 1.0f ), inverse );
	const XMVECTOR farPoint = XMVector3TransformCoord( XMVectorSet( ndcX, ndcY, 0.0f, 1.0f ), inverse );
	float distance = 0.0f;
	m_selected = models.pick( nearPoint, XMVector3Normalize( farPoint - nearPoint ), distance );
	if( m_selected >= 0 )
		gui.focusProperties( models.instanceProperties( m_selected ) );
	return m_selected;
}

void Viewport::drawSelection( const DMCamera& camera, const GS::ModelInstances& models ) const
{
	BoundingOrientedBox box;
	if( m_selected < 0 || !models.instanceBox( m_selected, box ) )
		return;

	// Углы рамки — на экран; угол за камерой — рамку не рисуем (линии через бесконечность)
	XMFLOAT3 corners[BoundingOrientedBox::CORNER_COUNT];
	box.GetCorners( corners );
	const XMMATRIX transform = viewProjection( camera );
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	ImVec2 points[BoundingOrientedBox::CORNER_COUNT];
	for( size_t i = 0; i < BoundingOrientedBox::CORNER_COUNT; ++i )
	{
		XMFLOAT4 clip;
		XMStoreFloat4( &clip, XMVector4Transform( XMVectorSet( corners[i].x, corners[i].y, corners[i].z, 1.0f ), transform ) );
		if( clip.w <= 1e-3f )
			return;
		points[i] = ImVec2( ( clip.x / clip.w * 0.5f + 0.5f ) * size.x, ( 0.5f - clip.y / clip.w * 0.5f ) * size.y );
	}

	// Порядок углов DirectXCollision: 0…3 — грань +Z, 4…7 — грань −Z
	static const int edges[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
									  { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
	// Под окнами редактора: рамка видна только над сценой
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	const ImU32 color = IM_COL32( 255, 170, 40, 255 );
	for( const auto& edge : edges )
		drawList->AddLine( points[edge[0]], points[edge[1]], color, 2.0f );
	ImVec2 top = points[0];
	for( const ImVec2& point : points )
		top = point.y < top.y ? point : top;
	drawList->AddText( ImVec2( top.x, top.y - ImGui::GetFontSize() - 2.0f ), color, models.instanceName( m_selected ).c_str() );
}
