#include "Viewport.h"
#include <DirectXCollision.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "GUI.h"
#include "Scene\Camera\DMCamera.h"
#include "Scene\Model\ModelInstances.h"
#include "Scene\Terrain\TerrainHeightSource.h"
#include "Scene\Water\WaterSimulation.h"

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

// Кривые русел видны ближе этого, м; точка ловится кликом ближе этого, пиксели
constexpr float streamDrawDistance = 250.0f;
constexpr float streamPickPixels = 10.0f;

// Точка мира на экран (пиксели окна); false — за камерой
bool toScreen( const XMMATRIX& transform, float x, float y, float z, ImVec2& screen )
{
	XMFLOAT4 clip;
	XMStoreFloat4( &clip, XMVector4Transform( XMVectorSet( x, y, z, 1.0f ), transform ) );
	if( clip.w <= 1e-3f )
		return false;
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	screen = ImVec2( ( clip.x / clip.w * 0.5f + 0.5f ) * size.x, ( 0.5f - clip.y / clip.w * 0.5f ) * size.y );
	return true;
}

// Высота точки кривой для показа — над поверхностью земли (дно русла), чтобы точку не закрывала вода
float streamPointHeight( const GS::TerrainHeightSource& terrain, float x, float z )
{
	float height = 0.0f;
	terrain.surfaceHeight( x, z, height );
	return height + 0.3f;
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

void Viewport::setStreamMode( bool enabled )
{
	m_streamMode = enabled;
	m_streamCurve = -1;
	m_streamPoint = -1;
}

void Viewport::update( const DMCamera& camera, GS::ModelInstances& models, GUI& gui, GS::WaterSimulation* water,
					   const GS::TerrainHeightSource* terrain )
{
	if( m_selected >= static_cast<int>( models.instanceCount() ) )
		m_selected = -1;
	const bool streams = m_streamMode && water && terrain;
	if( streams && ( m_streamCurve >= static_cast<int>( water->curves().size() ) ||
					 ( m_streamCurve >= 0 && m_streamPoint >= static_cast<int>( water->curves()[m_streamCurve].points.size() ) ) ) )
	{
		m_streamCurve = -1;
		m_streamPoint = -1;
	}

	ImGuizmo::BeginFrame();
	if( sceneClicked() )
		clickAt( camera, models, gui, water, terrain, ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y );
	if( streams )
	{
		drawStreams( camera, *water, *terrain );
		if( m_streamCurve >= 0 )
			editStreamPoint( camera, *water, *terrain, gui );
	}
	drawSelection( camera, models );
	if( m_selected >= 0 )
	{
		drawToolbar();
		manipulate( camera, models );
	}
}

std::string Viewport::clickAt( const DMCamera& camera, GS::ModelInstances& models, GUI& gui, GS::WaterSimulation* water,
								const GS::TerrainHeightSource* terrain, float x, float y )
{
	// В режиме кривых клик сначала ловит точку кривой, мимо — экземпляр модели
	if( m_streamMode && water && terrain && pickStreamPoint( camera, *water, *terrain, x, y ) )
	{
		m_selected = -1;
		if( PropertyContainer* properties = water->curveProperties( m_streamCurve ) )
			gui.focusProperties( properties );
		return water->curves()[m_streamCurve].name + ", point " + std::to_string( m_streamPoint );
	}
	m_streamCurve = -1;
	m_streamPoint = -1;
	const int instance = pickAt( camera, models, gui, x, y );
	return instance >= 0 ? models.instanceName( instance ) : std::string();
}

bool Viewport::moveStreamPoint( GS::WaterSimulation& water, float x, float z )
{
	if( m_streamCurve < 0 || m_streamPoint < 0 )
		return false;
	m_dragStart.clear();
	for( const GS::StreamCurvePoint& point : water.curves()[m_streamCurve].points )
		m_dragStart.push_back( point.position );
	dragStreamPoint( water, DirectX::XMFLOAT2( x, z ) );
	water.finishCurveEdit( m_streamCurve );
	return true;
}

void Viewport::dragStreamPoint( GS::WaterSimulation& water, DirectX::XMFLOAT2 position )
{
	const size_t count = m_dragStart.size();
	const size_t selected = static_cast<size_t>( m_streamPoint );
	if( selected >= count )
		return;
	const float dx = position.x - m_dragStart[selected].x;
	const float dz = position.y - m_dragStart[selected].y;
	// Расстояние по кривой от выбранной точки — в обе стороны
	std::vector<float> along( count, 0.0f );
	for( size_t i = selected + 1; i < count; ++i )
		along[i] = along[i - 1] + std::hypot( m_dragStart[i].x - m_dragStart[i - 1].x, m_dragStart[i].y - m_dragStart[i - 1].y );
	for( size_t i = selected; i-- > 0; )
		along[i] = along[i + 1] + std::hypot( m_dragStart[i].x - m_dragStart[i + 1].x, m_dragStart[i].y - m_dragStart[i + 1].y );
	for( size_t i = 0; i < count; ++i )
	{
		float weight = i == selected ? 1.0f : 0.0f;
		if( i != selected && m_streamFalloff > 0.0f && along[i] < m_streamFalloff )
		{
			const float t = along[i] / m_streamFalloff;
			weight = 1.0f - t * t * ( 3.0f - 2.0f * t );
		}
		if( weight > 0.0f || i == selected )
			water.setCurvePoint( m_streamCurve, i, DirectX::XMFLOAT2( m_dragStart[i].x + dx * weight, m_dragStart[i].y + dz * weight ) );
	}
}

bool Viewport::insertStreamPoint( GS::WaterSimulation& water )
{
	if( m_streamCurve < 0 || m_streamPoint < 0 )
		return false;
	// Посередине до следующей; у последней — продолжением кривой
	const GS::StreamCurve& curve = water.curves()[m_streamCurve];
	const DirectX::XMFLOAT2 point = curve.points[m_streamPoint].position;
	DirectX::XMFLOAT2 position;
	if( static_cast<size_t>( m_streamPoint ) + 1 < curve.points.size() )
	{
		const DirectX::XMFLOAT2 next = curve.points[m_streamPoint + 1].position;
		position = DirectX::XMFLOAT2( 0.5f * ( point.x + next.x ), 0.5f * ( point.y + next.y ) );
	}
	else
	{
		const DirectX::XMFLOAT2 before = curve.points[m_streamPoint - 1].position;
		position = DirectX::XMFLOAT2( 2.0f * point.x - before.x, 2.0f * point.y - before.y );
	}
	if( !water.insertCurvePoint( m_streamCurve, m_streamPoint, position ) )
		return false;
	++m_streamPoint;
	return true;
}

bool Viewport::removeStreamPoint( GS::WaterSimulation& water )
{
	if( m_streamCurve < 0 || m_streamPoint < 0 || !water.removeCurvePoint( m_streamCurve, m_streamPoint ) )
		return false;
	m_streamPoint = std::max( m_streamPoint - 1, 0 );
	return true;
}

bool Viewport::pickStreamPoint( const DMCamera& camera, const GS::WaterSimulation& water, const GS::TerrainHeightSource& terrain, float x, float y )
{
	const XMMATRIX transform = viewProjection( camera );
	const XMFLOAT3& eye = camera.position();
	float best = streamPickPixels;
	int bestCurve = -1;
	int bestPoint = -1;
	const std::vector<GS::StreamCurve>& curves = water.curves();
	for( size_t c = 0; c < curves.size(); ++c )
	{
		for( size_t i = 0; i < curves[c].points.size(); ++i )
		{
			const DirectX::XMFLOAT2 point = curves[c].points[i].position;
			if( std::hypot( point.x - eye.x, point.y - eye.z ) > streamDrawDistance )
				continue;
			ImVec2 screen;
			if( !toScreen( transform, point.x, streamPointHeight( terrain, point.x, point.y ), point.y, screen ) )
				continue;
			const float distance = std::hypot( screen.x - x, screen.y - y );
			if( distance < best )
			{
				best = distance;
				bestCurve = static_cast<int>( c );
				bestPoint = static_cast<int>( i );
			}
		}
	}
	if( bestCurve < 0 )
		return false;
	m_streamCurve = bestCurve;
	m_streamPoint = bestPoint;
	return true;
}

void Viewport::drawStreams( const DMCamera& camera, const GS::WaterSimulation& water, const GS::TerrainHeightSource& terrain ) const
{
	const XMMATRIX transform = viewProjection( camera );
	const XMFLOAT3& eye = camera.position();
	// Под окнами редактора, как рамка выбранного: видно только над сценой
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	const std::vector<GS::StreamCurve>& curves = water.curves();
	for( size_t c = 0; c < curves.size(); ++c )
	{
		const GS::StreamCurve& curve = curves[c];
		const bool selected = static_cast<int>( c ) == m_streamCurve;
		// Выбранная — оранжевая, правленная и ручная — голубая, сгенерированная — белая, выключенная — серая
		const ImU32 color = selected ? IM_COL32( 255, 170, 40, 255 ) : !curve.enabled ? IM_COL32( 140, 140, 140, 160 ) :
							curve.edited || !curve.generated ? IM_COL32( 90, 200, 255, 220 ) : IM_COL32( 235, 235, 235, 200 );
		ImVec2 previous;
		bool hasPrevious = false;
		for( size_t i = 0; i < curve.points.size(); ++i )
		{
			const DirectX::XMFLOAT2 point = curve.points[i].position;
			ImVec2 screen;
			if( std::hypot( point.x - eye.x, point.y - eye.z ) > streamDrawDistance ||
				!toScreen( transform, point.x, streamPointHeight( terrain, point.x, point.y ), point.y, screen ) )
			{
				hasPrevious = false;
				continue;
			}
			if( hasPrevious )
				drawList->AddLine( previous, screen, color, selected ? 2.5f : 1.5f );
			const bool current = selected && static_cast<int>( i ) == m_streamPoint;
			drawList->AddCircleFilled( screen, current ? 7.0f : selected ? 4.5f : 3.0f, current ? IM_COL32( 255, 80, 40, 255 ) : color );
			previous = screen;
			hasPrevious = true;
		}
	}
}

void Viewport::editStreamPoint( const DMCamera& camera, GS::WaterSimulation& water, const GS::TerrainHeightSource& terrain, GUI& gui )
{
	const GS::StreamCurve& curve = water.curves()[m_streamCurve];
	const DirectX::XMFLOAT2 point = curve.points[m_streamPoint].position;

	// Клавиши: Insert — точка после выбранной, Delete — удалить
	const ImGuiIO& io = ImGui::GetIO();
	if( !io.WantTextInput && !m_draggingPoint )
	{
		if( ImGui::IsKeyPressed( ImGuiKey_Insert, false ) )
		{
			insertStreamPoint( water );
			return;
		}
		if( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) )
		{
			if( !removeStreamPoint( water ) )
				gui.notify( "A stream curve keeps two points at least", true );
			return;
		}
	}

	// Подсказка над сценой
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + ImGui::GetFrameHeight() * 2.0f ),
							 ImGuiCond_Always, ImVec2( 0.5f, 0.0f ) );
	ImGui::SetNextWindowBgAlpha( 0.7f );
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
								   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
								   ImGuiWindowFlags_NoMove;
	if( ImGui::Begin( "##stream toolbar", nullptr, flags ) )
	{
		ImGui::Text( "%s, point %d of %d: drag the gizmo - move, Insert - add after, Delete - remove", curve.name.c_str(), m_streamPoint + 1,
					 static_cast<int>( curve.points.size() ) );
		ImGui::SetNextItemWidth( 160.0f );
		ImGui::SliderFloat( "Falloff", &m_streamFalloff, 0.0f, 60.0f, "%.0f m" );
		ImGui::SetItemTooltip( "Neighbour points along the curve this close move too, less towards the edge (0 - the point only)" );
	}
	ImGui::End();

	// Гизмо: перенос по X и Z; высота — земля под точкой
	XMMATRIX view;
	XMMATRIX projection;
	camera.viewMatrix( &view );
	camera.projectionMatrix( &projection );
	XMFLOAT4X4 viewData;
	XMFLOAT4X4 projectionData;
	XMFLOAT4X4 world;
	XMStoreFloat4x4( &viewData, view );
	XMStoreFloat4x4( &projectionData, projection );
	XMStoreFloat4x4( &world, XMMatrixTranslation( point.x, streamPointHeight( terrain, point.x, point.y ), point.y ) );
	ImGuizmo::SetOrthographic( false );
	ImGuizmo::SetDrawlist( ImGui::GetBackgroundDrawList() );
	ImGuizmo::SetRect( 0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y );
	ImGuizmo::Enable( ImGuizmo::IsUsing() || !overPanels() );
	const bool manipulated = ImGuizmo::Manipulate( &viewData._11, &projectionData._11,
												   static_cast<ImGuizmo::OPERATION>( ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Z ), ImGuizmo::WORLD,
												   &world._11 );
	// Начало перетаскивания — положения точек для мягкого выделения
	const bool using_ = ImGuizmo::IsUsing();
	if( using_ && !m_draggingPoint )
	{
		m_dragStart.clear();
		for( const GS::StreamCurvePoint& curvePoint : curve.points )
			m_dragStart.push_back( curvePoint.position );
	}
	if( manipulated && using_ )
		dragStreamPoint( water, DirectX::XMFLOAT2( world._41, world._43 ) );
	// Отпустили гизмо — правка закончена: русла перестраиваются
	if( m_draggingPoint && !using_ )
		water.finishCurveEdit( m_streamCurve );
	m_draggingPoint = using_;
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
