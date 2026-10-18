#include "Viewport.h"
#include <DirectXCollision.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "GUI.h"
#include "PropertyWidgets.h"
#include "Scene\Camera\DMCamera.h"
#include "Scene\Light\DMLightDriver.h"
#include "Scene\Light\SunPosition.h"
#include "Scene\Model\ModelInstances.h"
#include "Scene\Particle\ParticleSystem.h"
#include "Scene\Scene.h"
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

// Источники света: значок ловится кликом ближе этого, пиксели; значок направленного — в небе на этом расстоянии от камеры
// (ближе дальней плоскости)
constexpr float lightPickPixels = 14.0f;
constexpr float lightSkyDistance = 1000.0f;

// Луч из камеры через точку окна (пиксели), нормированный; начало — на ближней плоскости
void screenRay( const DMCamera& camera, float x, float y, XMVECTOR& origin, XMVECTOR& direction )
{
	// Точки на ближней (z = 1, глубина обратная) и дальней (z = 0) плоскостях
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	const float ndcX = 2.0f * x / size.x - 1.0f;
	const float ndcY = 1.0f - 2.0f * y / size.y;
	const XMMATRIX inverse = XMMatrixInverse( nullptr, viewProjection( camera ) );
	origin = XMVector3TransformCoord( XMVectorSet( ndcX, ndcY, 1.0f, 1.0f ), inverse );
	const XMVECTOR farPoint = XMVector3TransformCoord( XMVectorSet( ndcX, ndcY, 0.0f, 1.0f ), inverse );
	direction = XMVector3Normalize( farPoint - origin );
}

// Цвет значка: цвет источника или частиц, приведённый к яркости 1 (тёплое солнце — тёплое), выключенного — серый
ImU32 iconColor( const XMFLOAT3& color, bool enabled )
{
	if( !enabled )
		return IM_COL32( 140, 140, 140, 200 );
	const float peak = std::max( { color.x, color.y, color.z, 1e-4f } );
	return ImGui::ColorConvertFloat4ToU32( ImVec4( color.x / peak, color.y / peak, color.z / peak, 1.0f ) );
}

ImU32 lightColor( const DMLight& light )
{
	return iconColor( light.color(), light.enabled() );
}

// Выбранный значок: обводка и подпись
const ImU32 selectedColor = IM_COL32( 255, 170, 40, 255 );

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

void Viewport::setPaintMode( bool enabled )
{
	m_paintMode = enabled;
	if( enabled )
		clearSelection();
}

void Viewport::paintTerrain( const DMCamera& camera, GS::TerrainPaintTool& paint, const GS::TerrainPipeline& terrain, GUI& gui )
{
	const ImGuiIO& io = ImGui::GetIO();
	const GS::TerrainPaintTool::Brush brush = paint.brush( io.KeyShift, io.KeyCtrl );
	// Точка под курсором — луч к рельефу; над окнами редактора кисти нет
	XMFLOAT3 point = {};
	const bool hit = !overPanels() && terrainPoint( camera, terrain, io.MousePos.x, io.MousePos.y, point );

	// Мазок: начало — ЛКМ над сценой, касания — каждый кадр (с промежуточными, если мышь ушла дальше ¼ радиуса), конец —
	// ЛКМ отпустили
	if( !m_painting && hit && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !brush.target.empty() )
	{
		std::string reason;
		m_painting = paint.beginStroke( brush.target, brush.mode, brush.value, reason );
		if( m_painting )
		{
			paint.dab( point.x, point.z, brush.radius, brush.falloff );
			m_lastDab = XMFLOAT2( point.x, point.z );
		}
		else
			LOG( reason );
	}
	else if( m_painting && ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
	{
		if( hit )
			dabTo( paint, brush.radius, brush.falloff, point );
	}
	else if( m_painting )
		finishPaintStroke( paint, gui );

	if( !hit )
		return;
	// Круг кисти по рельефу и мягкий край; цвет — режим: белый — значение, зелёный — шаг, красный — убавить, серый — стереть
	const ImU32 color = brush.mode == GS::TerrainPaint::Mode::value ? IM_COL32( 255, 255, 255, 220 ) :
						brush.mode == GS::TerrainPaint::Mode::step ? IM_COL32( 90, 230, 90, 220 ) :
						brush.mode == GS::TerrainPaint::Mode::unstep ? IM_COL32( 240, 90, 70, 220 ) : IM_COL32( 160, 160, 160, 220 );
	const XMMATRIX transform = viewProjection( camera );
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	for( const float scale : { 1.0f, 1.0f - brush.falloff } )
	{
		if( scale <= 0.02f )
			continue;
		const float radius = brush.radius * scale;
		constexpr int segments = 64;
		ImVec2 previous;
		bool hasPrevious = false;
		for( int i = 0; i <= segments; ++i )
		{
			const float angle = XM_2PI * i / segments;
			const float x = point.x + radius * std::cos( angle );
			const float z = point.z + radius * std::sin( angle );
			float height = point.y;
			terrain.surfaceHeight( x, z, height );
			ImVec2 screen;
			if( !toScreen( transform, x, height + 0.1f, z, screen ) )
			{
				hasPrevious = false;
				continue;
			}
			if( hasPrevious )
				drawList->AddLine( previous, screen, color, scale == 1.0f ? 2.0f : 1.0f );
			previous = screen;
			hasPrevious = true;
		}
	}
	char label[160];
	const char* mode = brush.mode == GS::TerrainPaint::Mode::value ? "value" : brush.mode == GS::TerrainPaint::Mode::step ? "+" :
					   brush.mode == GS::TerrainPaint::Mode::unstep ? "-" : "erase";
	std::snprintf( label, sizeof( label ), "%s  %s %.2f", brush.target.c_str(), mode, brush.value );
	drawList->AddText( ImVec2( io.MousePos.x + 16.0f, io.MousePos.y + 12.0f ), color, label );
}

bool Viewport::terrainPoint( const DMCamera& camera, const GS::TerrainPipeline& terrain, float x, float y, XMFLOAT3& point ) const
{
	XMVECTOR origin, direction;
	screenRay( camera, x, y, origin, direction );
	XMFLOAT3 o, d;
	XMStoreFloat3( &o, origin );
	XMStoreFloat3( &d, direction );
	float distance = 0.0f;
	if( !terrain.raycast( o, d, camera.farPlane(), distance ) )
		return false;
	point = XMFLOAT3( o.x + d.x * distance, o.y + d.y * distance, o.z + d.z * distance );
	return true;
}

void Viewport::dabTo( GS::TerrainPaintTool& paint, float radius, float falloff, const XMFLOAT3& point )
{
	// Между касаниями — промежуточные с шагом в ¼ радиуса: быстрый взмах мышью — сплошной мазок, а не пятна
	const float step = std::max( radius * 0.25f, 0.25f );
	const float dx = point.x - m_lastDab.x;
	const float dz = point.z - m_lastDab.y;
	const int count = std::min( static_cast<int>( std::hypot( dx, dz ) / step ), 256 );
	for( int i = 1; i <= count; ++i )
		paint.dab( m_lastDab.x + dx * i / count, m_lastDab.y + dz * i / count, radius, falloff );
	if( count > 0 )
		m_lastDab = XMFLOAT2( point.x, point.z );
}

std::string Viewport::brushStroke( const DMCamera& camera, GS::TerrainPaintTool& paint, const GS::TerrainPipeline& terrain, GUI& gui,
								   const std::vector<ImVec2>& points, bool shift, bool ctrl )
{
	if( m_painting )
		return "error: a stroke is going on";
	const GS::TerrainPaintTool::Brush brush = paint.brush( shift, ctrl );
	std::vector<XMFLOAT3> hits;
	for( const ImVec2& screen : points )
	{
		XMFLOAT3 point;
		if( terrainPoint( camera, terrain, screen.x, screen.y, point ) )
			hits.push_back( point );
	}
	if( hits.empty() )
		return "error: the brush misses the terrain";
	std::string reason;
	if( brush.target.empty() || !paint.beginStroke( brush.target, brush.mode, brush.value, reason ) )
		return "error: " + ( reason.empty() ? std::string( "no brush target" ) : reason );
	m_painting = true;
	paint.dab( hits[0].x, hits[0].z, brush.radius, brush.falloff );
	m_lastDab = XMFLOAT2( hits[0].x, hits[0].z );
	for( size_t i = 1; i < hits.size(); ++i )
		dabTo( paint, brush.radius, brush.falloff, hits[i] );
	finishPaintStroke( paint, gui );
	char text[160];
	std::snprintf( text, sizeof( text ), "%s from %.1f, %.1f to %.1f, %.1f", brush.target.c_str(), hits.front().x, hits.front().z, hits.back().x,
				   hits.back().z );
	return text;
}

void Viewport::finishPaintStroke( GS::TerrainPaintTool& paint, GUI& gui )
{
	m_painting = false;
	const GS::TerrainPaintTool::Edit edit = paint.endStroke();
	if( !edit.changed )
		return;
	// Отмена и повтор — плитки цели целиком
	auto shared = std::make_shared<GS::TerrainPaintTool::Edit>( edit );
	GS::TerrainPaintTool* target = &paint;
	gui.history().record( "Paint " + edit.target, [target, shared] { target->setTiles( shared->target, shared->before ); },
						  [target, shared] { target->setTiles( shared->target, shared->after ); } );
}

void Viewport::setStreamMode( bool enabled )
{
	m_streamMode = enabled;
	m_streamCurve = -1;
	m_streamPoint = -1;
}

void Viewport::update( const DMCamera& camera, GS::Scene& scene, GUI& gui )
{
	GS::ModelInstances& models = scene.models();
	GS::StreamCurves* water = scene.water().initialized() ? &scene.water().streams() : nullptr;
	const GS::TerrainHeightSource* terrain = &scene.terrainPipeline();
	DMLightDriver* lights = &scene.lights();
	GS::ParticleSystem* particles = scene.particles().initialized() ? &scene.particles() : nullptr;
	// Экземпляр и источник — по ключу: номер мог сдвинуться, сам выбранный — убран
	m_selected = models.indexOf( m_selectedKey );
	if( m_selected < 0 )
		m_selectedKey = 0;
	m_selectedLight = lights->indexOf( m_selectedLightKey );
	if( m_selectedLight < 0 )
		m_selectedLightKey = 0;
	if( !particles || m_selectedEmitter >= static_cast<int>( particles->emitterCount() ) )
		m_selectedEmitter = -1;
	const bool streams = m_streamMode && water && terrain;
	if( streams && ( m_streamCurve >= static_cast<int>( water->curves().size() ) ||
					 ( m_streamCurve >= 0 && m_streamPoint >= static_cast<int>( water->curves()[m_streamCurve].points.size() ) ) ) )
	{
		m_streamCurve = -1;
		m_streamPoint = -1;
	}

	ImGuizmo::BeginFrame();
	// Кисть: клик по сцене — мазок, а не выбор
	if( m_paintMode )
	{
		paintTerrain( camera, scene.terrainPaint(), scene.terrainPipeline(), gui );
		sceneClicked();
	}
	else
	{
		if( m_painting )
			finishPaintStroke( scene.terrainPaint(), gui );
		if( sceneClicked() )
			clickAt( camera, scene, gui, ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y );
	}
	drawCollision( camera, scene );
	drawLights( camera, *lights );
	if( particles )
		drawEmitters( camera, *particles );
	if( streams )
	{
		drawStreams( camera, *water, *terrain );
		if( m_streamCurve >= 0 )
			editStreamPoint( camera, *water, *terrain, gui );
	}
	drawSelection( camera, models );
	// Delete — кнопка Delete подокна выбранного экземпляра или источника: убрать с отменой, как в Details
	PropertyContainer* removable = m_selected >= 0 ? models.instanceProperties( m_selected ) :
								   m_selectedLight >= 0 ? lights->lightProperties( m_selectedLight ) : nullptr;
	const char* deleteButton = m_selected >= 0 ? GS::ModelInstances::deleteButton : DMLightDriver::deleteButton;
	if( removable && !m_usingGizmo && !m_draggingLight && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed( ImGuiKey_Delete, false ) )
	{
		gui.press( *removable, deleteButton );
		clearSelection();
		trackGizmo( false, gui, {}, {} );
		return;
	}
	if( m_selected >= 0 )
	{
		drawToolbar();
		manipulate( camera, models, gui );
	}
	else if( m_selectedLight >= 0 )
		editLight( camera, *lights, gui );
	else if( m_selectedEmitter >= 0 )
		editEmitter( camera, *particles, gui );
	else
		trackGizmo( false, gui, {}, {} );
}

void Viewport::clearSelection()
{
	m_selected = -1;
	m_selectedKey = 0;
	m_selectedLight = -1;
	m_selectedLightKey = 0;
	m_selectedEmitter = -1;
	m_streamCurve = -1;
	m_streamPoint = -1;
}

void Viewport::selectLight( const DMLightDriver& lights, uint32_t key )
{
	clearSelection();
	m_selectedLightKey = key;
	m_selectedLight = lights.indexOf( key );
}

void Viewport::selectInstance( const GS::ModelInstances& models, uint32_t key )
{
	clearSelection();
	m_selectedKey = key;
	m_selected = models.indexOf( key );
}

std::string Viewport::clickAt( const DMCamera& camera, GS::Scene& scene, GUI& gui, float x, float y )
{
	GS::ModelInstances& models = scene.models();
	GS::StreamCurves* water = scene.water().initialized() ? &scene.water().streams() : nullptr;
	DMLightDriver& lights = scene.lights();
	GS::ParticleSystem* particles = scene.particles().initialized() ? &scene.particles() : nullptr;
	clearSelection();
	// В режиме кривых клик сначала ловит точку кривой, мимо — экземпляр модели
	if( m_streamMode && water && pickStreamPoint( camera, *water, scene.terrainPipeline(), x, y ) )
	{
		if( PropertyContainer* properties = water->curveProperties( m_streamCurve ) )
			gui.focusProperties( properties );
		return water->curves()[m_streamCurve].name + ", point " + std::to_string( m_streamPoint );
	}
	// Значки источников света и эмиттеров — раньше экземпляров: значок мал и рисуется поверх сцены. Из двух — ближайший к
	// точке (у костра источник и искры стоят рядом)
	float lightDistance = 0.0f;
	float emitterDistance = 0.0f;
	const int light = pickLight( camera, lights, x, y, lightDistance );
	const int emitter = particles ? pickEmitter( camera, *particles, x, y, emitterDistance ) : -1;
	if( emitter >= 0 && ( light < 0 || emitterDistance < lightDistance ) )
	{
		m_selectedEmitter = emitter;
		PropertyContainer* properties = particles->emitterProperties( emitter );
		gui.focusProperties( properties );
		return properties->name();
	}
	if( light >= 0 )
	{
		m_selectedLight = light;
		m_selectedLightKey = lights.key( light );
		PropertyContainer* properties = lights.lightProperties( light );
		if( properties )
			gui.focusProperties( properties );
		return properties ? properties->name() : lights.lights()[light].name;
	}
	const int instance = pickAt( camera, models, gui, x, y );
	return instance >= 0 ? models.instanceName( instance ) : std::string();
}

bool Viewport::moveStreamPoint( GS::StreamCurves& streams, float x, float z )
{
	if( m_streamCurve < 0 || m_streamPoint < 0 )
		return false;
	m_dragStart.clear();
	for( const GS::StreamCurvePoint& point : streams.curves()[m_streamCurve].points )
		m_dragStart.push_back( point.position );
	dragStreamPoint( streams, DirectX::XMFLOAT2( x, z ) );
	streams.finishCurveEdit( m_streamCurve );
	return true;
}

void Viewport::dragStreamPoint( GS::StreamCurves& streams, DirectX::XMFLOAT2 position )
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
			streams.setCurvePoint( m_streamCurve, i, DirectX::XMFLOAT2( m_dragStart[i].x + dx * weight, m_dragStart[i].y + dz * weight ) );
	}
}

bool Viewport::insertStreamPoint( GS::StreamCurves& streams )
{
	if( m_streamCurve < 0 || m_streamPoint < 0 )
		return false;
	// Посередине до следующей; у последней — продолжением кривой
	const GS::StreamCurve& curve = streams.curves()[m_streamCurve];
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
	if( !streams.insertCurvePoint( m_streamCurve, m_streamPoint, position ) )
		return false;
	++m_streamPoint;
	return true;
}

bool Viewport::removeStreamPoint( GS::StreamCurves& streams )
{
	if( m_streamCurve < 0 || m_streamPoint < 0 || !streams.removeCurvePoint( m_streamCurve, m_streamPoint ) )
		return false;
	m_streamPoint = std::max( m_streamPoint - 1, 0 );
	return true;
}

bool Viewport::pickStreamPoint( const DMCamera& camera, const GS::StreamCurves& streams, const GS::TerrainHeightSource& terrain, float x, float y )
{
	const XMMATRIX transform = viewProjection( camera );
	const XMFLOAT3& eye = camera.position();
	float best = streamPickPixels;
	int bestCurve = -1;
	int bestPoint = -1;
	const std::vector<GS::StreamCurve>& curves = streams.curves();
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

void Viewport::drawStreams( const DMCamera& camera, const GS::StreamCurves& streams, const GS::TerrainHeightSource& terrain ) const
{
	const XMMATRIX transform = viewProjection( camera );
	const XMFLOAT3& eye = camera.position();
	// Под окнами редактора, как рамка выбранного: видно только над сценой
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	const std::vector<GS::StreamCurve>& curves = streams.curves();
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

void Viewport::editStreamPoint( const DMCamera& camera, GS::StreamCurves& streams, const GS::TerrainHeightSource& terrain, GUI& gui )
{
	const GS::StreamCurve& curve = streams.curves()[m_streamCurve];
	const DirectX::XMFLOAT2 point = curve.points[m_streamPoint].position;

	// Клавиши: Insert — точка после выбранной, Delete — удалить
	const ImGuiIO& io = ImGui::GetIO();
	if( !io.WantTextInput && !m_draggingPoint )
	{
		if( ImGui::IsKeyPressed( ImGuiKey_Insert, false ) )
		{
			insertStreamPoint( streams );
			return;
		}
		if( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) )
		{
			if( !removeStreamPoint( streams ) )
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
		PropertyWidgets::NumberOptions falloff;
		falloff.high = 60.0f;
		falloff.fill = true;
		falloff.step = 1.0f;
		PropertyWidgets::numberField( "Falloff", &m_streamFalloff, "%.0f m", falloff );
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
		dragStreamPoint( streams, DirectX::XMFLOAT2( world._41, world._43 ) );
	// Отпустили гизмо — правка закончена: русла перестраиваются
	if( m_draggingPoint && !using_ )
		streams.finishCurveEdit( m_streamCurve );
	m_draggingPoint = using_;
}

void Viewport::drawToolbar( bool rotate, bool scale, bool localAxes )
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
	// Режима нет у выбранного (масштаб у источника света) — перемещение
	if( ( m_gizmo == Gizmo::rotate && !rotate ) || ( m_gizmo == Gizmo::scale && !scale ) )
		m_gizmo = Gizmo::move;

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
		mode( "Move", Gizmo::move, "Move the selected (5)" );
		if( rotate )
			mode( "Rotate", Gizmo::rotate, "Rotate the selected (6)" );
		if( scale )
			mode( "Scale", Gizmo::scale, "Scale the selected instance (7)" );
		if( localAxes )
		{
			ImGui::Checkbox( "Local axes", &m_localAxes );
			ImGui::SetItemTooltip( "Axes of the instance instead of the world" );
		}
		else
			ImGui::NewLine();
	}
	ImGui::End();
}

void Viewport::manipulate( const DMCamera& camera, GS::ModelInstances& models, GUI& gui )
{
	// Свойства экземпляра до гизмо этого кадра: если его сейчас возьмут, это — «до» правки
	const std::vector<Property*> properties = Editor::EditHistory::collect( *models.instanceProperties( m_selected ) );
	snapshotGizmo( properties );

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
	// Перетаскивание гизмо — одна правка истории: от взятия до отпускания
	const char* action = m_gizmo == Gizmo::move ? "Move " : m_gizmo == Gizmo::rotate ? "Rotate " : "Scale ";
	trackGizmo( ImGuizmo::IsUsing(), gui, properties, action + models.instanceProperties( m_selected )->name() );
}

void Viewport::snapshotGizmo( const std::vector<Property*>& properties )
{
	if( m_usingGizmo )
		return;
	m_gizmoBefore.clear();
	for( const Property* property : properties )
		m_gizmoBefore.push_back( property->value() );
}

void Viewport::trackGizmo( bool using_, GUI& gui, const std::vector<Property*>& properties, const std::string& name )
{
	if( using_ && !m_usingGizmo )
		gui.history().begin( name, properties, &m_gizmoBefore );
	else if( !using_ && m_usingGizmo )
		gui.history().end();
	m_usingGizmo = using_;
}

bool Viewport::lightScreen( const DMCamera& camera, const DMLightDriver& lights, size_t index, XMFLOAT2& screen ) const
{
	const DMLight& light = lights.lights()[index];
	XMFLOAT3 at = light.position();
	if( light.type() == DMLight::Dir )
	{
		// Направленный — в небе, откуда идёт свет: против направления, далеко от камеры
		const XMFLOAT3& eye = camera.position();
		const XMFLOAT3 direction = light.direction();
		at = XMFLOAT3( eye.x - direction.x * lightSkyDistance, eye.y - direction.y * lightSkyDistance, eye.z - direction.z * lightSkyDistance );
	}
	ImVec2 point;
	if( !toScreen( viewProjection( camera ), at.x, at.y, at.z, point ) )
		return false;
	screen = XMFLOAT2( point.x, point.y );
	return true;
}

int Viewport::pickLight( const DMCamera& camera, const DMLightDriver& lights, float x, float y, float& distance ) const
{
	int nearest = -1;
	distance = lightPickPixels;
	for( size_t i = 0; i < lights.lights().size(); ++i )
	{
		XMFLOAT2 screen;
		if( !lightScreen( camera, lights, i, screen ) )
			continue;
		const float d = std::hypot( screen.x - x, screen.y - y );
		if( d < distance )
		{
			distance = d;
			nearest = static_cast<int>( i );
		}
	}
	return nearest;
}

void Viewport::drawCollision( const DMCamera& camera, GS::Scene& scene ) const
{
	PropertyContainer& world = *scene.worldEntities().properties();
	if( !world.exists( "Show collision" ) || !world["Show collision"].data<bool>() )
		return;
	constexpr float radius = 30.0f;	// в густом лесу — ~2 мс CPU кадра (в 40 м было ~7)
	std::vector<GS::PhysicsWorld::DebugLine> lines;
	scene.world().physics().debugLines( camera.position(), radius, lines );
	const XMMATRIX transform = viewProjection( camera );
	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	const ImU32 bodyColor = IM_COL32( 80, 230, 120, 200 );
	const ImU32 characterColor = IM_COL32( 255, 220, 60, 230 );
	for( const GS::PhysicsWorld::DebugLine& line : lines )
	{
		// Отрезок, у которого конец за камерой, — не рисуется (отладочный вид, без отсечения по ближней плоскости)
		ImVec2 from, to;
		if( toScreen( transform, line.from.x, line.from.y, line.from.z, from ) && toScreen( transform, line.to.x, line.to.y, line.to.z, to ) )
			draw->AddLine( from, to, line.character ? characterColor : bodyColor, 1.0f );
	}
}

void Viewport::drawLights( const DMCamera& camera, const DMLightDriver& lights ) const
{
	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	const XMMATRIX transform = viewProjection( camera );
	const ImU32 outline = IM_COL32( 0, 0, 0, 160 );
	for( size_t i = 0; i < lights.lights().size(); ++i )
	{
		XMFLOAT2 at;
		if( !lightScreen( camera, lights, i, at ) )
			continue;
		const DMLight& light = lights.lights()[i];
		const ImVec2 center( at.x, at.y );
		const ImU32 color = lightColor( light );
		const bool selected = static_cast<int>( i ) == m_selectedLight;
		if( light.type() == DMLight::Dir && light.atmosphereSunLightIndex() == 1 )
		{
			// Луна — серп: диск и тёмный диск со сдвигом
			draw->AddCircleFilled( center, 8.0f, outline );
			draw->AddCircleFilled( center, 7.0f, color );
			draw->AddCircleFilled( ImVec2( center.x + 3.5f, center.y - 2.5f ), 6.0f, IM_COL32( 20, 24, 36, 255 ) );
		}
		else if( light.type() == DMLight::Dir )
		{
			// Солнце — диск с лучами
			draw->AddCircleFilled( center, 7.0f, outline );
			draw->AddCircleFilled( center, 6.0f, color );
			for( int ray = 0; ray < 8; ++ray )
			{
				const float angle = ray * XM_PIDIV4;
				const ImVec2 from( center.x + std::cos( angle ) * 8.5f, center.y + std::sin( angle ) * 8.5f );
				const ImVec2 to( center.x + std::cos( angle ) * 12.5f, center.y + std::sin( angle ) * 12.5f );
				draw->AddLine( from, to, outline, 3.5f );
				draw->AddLine( from, to, color, 1.8f );
			}
		}
		else
		{
			// Точечный и прожектор — диск с кольцом; у прожектора — линия направления на 2 м
			if( light.type() == DMLight::Spot )
			{
				const XMFLOAT3& p = light.position();
				const XMFLOAT3 d = light.direction();
				ImVec2 tip;
				if( toScreen( transform, p.x + d.x * 2.0f, p.y + d.y * 2.0f, p.z + d.z * 2.0f, tip ) )
				{
					draw->AddLine( center, tip, outline, 4.0f );
					draw->AddLine( center, tip, color, 2.0f );
					draw->AddCircleFilled( tip, 3.0f, color );
				}
			}
			draw->AddCircleFilled( center, 6.0f, outline );
			draw->AddCircleFilled( center, 5.0f, color );
			draw->AddCircle( center, 9.0f, outline, 0, 3.0f );
			draw->AddCircle( center, 9.0f, color, 0, 1.5f );
		}
		if( selected )
		{
			draw->AddCircle( center, 15.0f, selectedColor, 0, 2.0f );
			const PropertyContainer* properties = const_cast<DMLightDriver&>( lights ).lightProperties( i );
			const std::string name = properties ? const_cast<PropertyContainer*>( properties )->name() : light.name;
			draw->AddText( ImVec2( center.x + 18.0f, center.y - 8.0f ), selectedColor, name.c_str() );
		}
	}
}

void Viewport::editLight( const DMCamera& camera, DMLightDriver& lights, GUI& gui )
{
	const DMLight& light = lights.lights()[m_selectedLight];
	PropertyContainer* properties = lights.lightProperties( m_selectedLight );
	if( !properties )
		return;
	if( light.type() == DMLight::Dir )
	{
		dragDirectionalLight( camera, lights, gui );
		return;
	}

	// Точечный и прожектор — гизмо: перенос, у прожектора ещё поворот (ось +Z гизмо — направление света)
	const bool spot = light.type() == DMLight::Spot;
	drawToolbar( spot, false, false );
	const std::vector<Property*> history = Editor::EditHistory::collect( *properties );
	snapshotGizmo( history );

	const XMVECTOR forward = XMVector3Normalize( XMLoadFloat3( &light.direction() ) );
	const XMVECTOR up = std::abs( XMVectorGetY( forward ) ) > 0.99f ? XMVectorSet( 1.0f, 0.0f, 0.0f, 0.0f ) : XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
	const XMVECTOR right = XMVector3Normalize( XMVector3Cross( up, forward ) );
	const XMVECTOR upAxis = XMVector3Cross( forward, right );
	XMMATRIX basis( right, upAxis, forward, XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f ) );
	basis.r[3] = XMVectorSetW( XMLoadFloat3( &light.position() ), 1.0f );

	XMFLOAT4X4 world;
	XMStoreFloat4x4( &world, basis );
	const bool rotating = spot && m_gizmo == Gizmo::rotate;
	if( manipulatePoint( camera, world, rotating ) )
	{
		( *properties )["Position"].setData( XMFLOAT3( world._41, world._42, world._43 ) );
		if( rotating )
		{
			XMFLOAT3 direction;
			XMStoreFloat3( &direction, XMVector3Normalize( XMVectorSet( world._31, world._32, world._33, 0.0f ) ) );
			const XMFLOAT2 rotation = DMLightDriver::rotationFromDirection( direction );
			( *properties )["Pitch"].setData( rotation.x );
			( *properties )["Yaw"].setData( rotation.y );
		}
	}
	trackGizmo( ImGuizmo::IsUsing(), gui, history, ( rotating ? "Rotate " : "Move " ) + properties->name() );
}

bool Viewport::manipulatePoint( const DMCamera& camera, XMFLOAT4X4& world, bool rotate )
{
	XMMATRIX view;
	XMMATRIX projection;
	camera.viewMatrix( &view );
	camera.projectionMatrix( &projection );
	XMFLOAT4X4 viewData;
	XMFLOAT4X4 projectionData;
	XMStoreFloat4x4( &viewData, view );
	XMStoreFloat4x4( &projectionData, projection );
	const ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetOrthographic( false );
	ImGuizmo::SetDrawlist( ImGui::GetBackgroundDrawList() );
	ImGuizmo::SetRect( 0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y );
	ImGuizmo::Enable( ImGuizmo::IsUsing() || !overPanels() );
	return ImGuizmo::Manipulate( &viewData._11, &projectionData._11, rotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE,
								 rotate ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &world._11 );
}

bool Viewport::emitterScreen( const DMCamera& camera, const GS::ParticleSystem& particles, size_t index, XMFLOAT2& screen ) const
{
	const GS::ParticleEmitterSettings settings = particles.emitterSettings( index );
	ImVec2 point;
	if( !GS::ParticleSystem::placed( settings ) ||
		!toScreen( viewProjection( camera ), settings.position.x, settings.position.y, settings.position.z, point ) )
		return false;
	screen = XMFLOAT2( point.x, point.y );
	return true;
}

int Viewport::pickEmitter( const DMCamera& camera, const GS::ParticleSystem& particles, float x, float y, float& distance ) const
{
	int nearest = -1;
	distance = lightPickPixels;
	for( size_t i = 0; i < particles.emitterCount(); ++i )
	{
		XMFLOAT2 screen;
		if( !emitterScreen( camera, particles, i, screen ) )
			continue;
		const float d = std::hypot( screen.x - x, screen.y - y );
		if( d < distance )
		{
			distance = d;
			nearest = static_cast<int>( i );
		}
	}
	return nearest;
}

void Viewport::drawEmitters( const DMCamera& camera, GS::ParticleSystem& particles ) const
{
	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	const XMMATRIX transform = viewProjection( camera );
	const ImU32 outline = IM_COL32( 0, 0, 0, 160 );
	for( size_t i = 0; i < particles.emitterCount(); ++i )
	{
		XMFLOAT2 at;
		if( !emitterScreen( camera, particles, i, at ) )
			continue;
		const GS::ParticleEmitterSettings settings = particles.emitterSettings( i );
		const ImVec2 center( at.x, at.y );
		const ImU32 color = iconColor( settings.color, particles.emitterEnabled( i ) );
		// Значок — брызги: точка и шесть точек вокруг (как спрайт эмиттера Niagara в UE)
		draw->AddCircleFilled( center, 4.5f, outline );
		draw->AddCircleFilled( center, 3.5f, color );
		for( int k = 0; k < 6; ++k )
		{
			const float angle = k * XM_PI / 3.0f + XM_PI / 6.0f;
			const ImVec2 dot( center.x + std::cos( angle ) * 9.0f, center.y + std::sin( angle ) * 9.0f );
			draw->AddCircleFilled( dot, 2.8f, outline );
			draw->AddCircleFilled( dot, 2.0f, color );
		}
		if( static_cast<int>( i ) != m_selectedEmitter )
			continue;
		// Шар рождения выбранного — три окружности по плоскостям мира
		if( settings.spawn == GS::ParticleEmitterSettings::Spawn::sphere && settings.radius > 0.0f )
		{
			constexpr int segments = 48;
			const XMFLOAT3& p = settings.position;
			for( int plane = 0; plane < 3; ++plane )
			{
				ImVec2 previous;
				bool hasPrevious = false;
				for( int s = 0; s <= segments; ++s )
				{
					const float angle = XM_2PI * s / segments;
					const float a = std::cos( angle ) * settings.radius;
					const float b = std::sin( angle ) * settings.radius;
					const XMFLOAT3 point = plane == 0 ? XMFLOAT3( p.x + a, p.y + b, p.z ) : plane == 1 ? XMFLOAT3( p.x + a, p.y, p.z + b ) :
																							XMFLOAT3( p.x, p.y + a, p.z + b );
					ImVec2 screen;
					const bool visible = toScreen( transform, point.x, point.y, point.z, screen );
					if( visible && hasPrevious )
						draw->AddLine( previous, screen, selectedColor, 1.5f );
					previous = screen;
					hasPrevious = visible;
				}
			}
		}
		draw->AddCircle( center, 15.0f, selectedColor, 0, 2.0f );
		draw->AddText( ImVec2( center.x + 18.0f, center.y - 8.0f ), selectedColor, particles.emitterProperties( i )->name().c_str() );
	}
}

void Viewport::editEmitter( const DMCamera& camera, GS::ParticleSystem& particles, GUI& gui )
{
	PropertyContainer* properties = particles.emitterProperties( m_selectedEmitter );
	if( !properties || !properties->exists( "Position" ) )
	{
		trackGizmo( false, gui, {}, {} );
		return;
	}
	// Гизмо перемещения: место пишется в свойство Position, как правка в Details
	drawToolbar( false, false, false );
	const std::vector<Property*> history = Editor::EditHistory::collect( *properties );
	snapshotGizmo( history );
	const XMFLOAT3 position = ( *properties )["Position"].data<XMFLOAT3>();
	XMFLOAT4X4 world;
	XMStoreFloat4x4( &world, XMMatrixTranslation( position.x, position.y, position.z ) );
	if( manipulatePoint( camera, world, false ) )
		( *properties )["Position"].setData( XMFLOAT3( world._41, world._42, world._43 ) );
	trackGizmo( ImGuizmo::IsUsing(), gui, history, "Move " + properties->name() );
}

void Viewport::dragDirectionalLight( const DMCamera& camera, DMLightDriver& lights, GUI& gui )
{
	// Направленный: значок в небе тянут мышью — свет идёт из-под курсора. Солнце и луну при SunPosition ведёт время
	// суток: тянут вдоль их дуги — время, при котором светило ближе всего к курсору
	const size_t index = static_cast<size_t>( m_selectedLight );
	PropertyContainer* properties = lights.lightProperties( index );
	PropertyContainer* sunPosition = lights.directionFromTime( index ) ? lights.sunPositionProperties() : nullptr;
	const bool byTime = sunPosition && lights.sunPosition();
	const ImGuiIO& io = ImGui::GetIO();

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + ImGui::GetFrameHeight() * 2.0f ),
							 ImGuiCond_Always, ImVec2( 0.5f, 0.0f ) );
	ImGui::SetNextWindowBgAlpha( 0.7f );
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
								   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
								   ImGuiWindowFlags_NoMove;
	if( ImGui::Begin( "##light toolbar", nullptr, flags ) )
		ImGui::TextUnformatted( byTime ? "Drag the icon along its path: Time of day follows" : "Drag the icon across the sky: the light comes from there" );
	ImGui::End();

	XMFLOAT2 icon;
	const bool visible = lightScreen( camera, lights, index, icon );
	if( !m_draggingLight && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !overPanels() && visible &&
		std::hypot( io.MousePos.x - icon.x, io.MousePos.y - icon.y ) < lightPickPixels )
	{
		m_draggingLight = true;
		const std::vector<Property*> edited = byTime ? std::vector<Property*>{ &( *sunPosition )["Time of day"] } :
													   std::vector<Property*>{ &( *properties )["Pitch"], &( *properties )["Yaw"] };
		gui.history().begin( ( byTime ? "Time of day: " : "Rotate " ) + properties->name(), edited );
	}
	if( !m_draggingLight )
		return;
	if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
	{
		m_draggingLight = false;
		gui.history().end();
		return;
	}
	ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeAll );
	XMVECTOR origin;
	XMVECTOR ray;
	screenRay( camera, io.MousePos.x, io.MousePos.y, origin, ray );
	XMFLOAT3 toLight;
	XMStoreFloat3( &toLight, ray );
	if( !byTime )
	{
		const XMFLOAT2 rotation = DMLightDriver::rotationFromDirection( XMFLOAT3( -toLight.x, -toLight.y, -toLight.z ) );
		( *properties )["Pitch"].setData( rotation.x );
		( *properties )["Yaw"].setData( rotation.y );
		return;
	}
	// Время суток, при котором светило ближе всего к лучу: сутки по 5 минут, затем вокруг лучшего по 10 секунд
	SunPosition::Settings settings = lights.sunPosition()->settings();
	const bool moon = lights.moonFromTime( index );
	auto closeness = [&]( double hours )
	{
		settings.timeOfDay = static_cast<float>( std::fmod( hours + 24.0, 24.0 ) );
		const SunPosition::Angles angles = moon ? SunPosition::computeMoon( settings ).angles : SunPosition::compute( settings );
		const XMFLOAT3 body = SunPosition::direction( angles, settings.northOffset );
		return body.x * toLight.x + body.y * toLight.y + body.z * toLight.z;
	};
	double best = 0.0;
	double bestCloseness = -2.0;
	for( double hours = 0.0; hours < 24.0; hours += 1.0 / 12.0 )
	{
		const double c = closeness( hours );
		if( c > bestCloseness )
		{
			bestCloseness = c;
			best = hours;
		}
	}
	const double coarse = best;
	for( double hours = coarse - 1.0 / 12.0; hours <= coarse + 1.0 / 12.0; hours += 1.0 / 360.0 )
	{
		const double c = closeness( hours );
		if( c > bestCloseness )
		{
			bestCloseness = c;
			best = hours;
		}
	}
	( *sunPosition )["Time of day"].setData( static_cast<float>( std::fmod( best + 24.0, 24.0 ) ) );
}

int Viewport::pickAt( const DMCamera& camera, GS::ModelInstances& models, GUI& gui, float x, float y )
{
	XMVECTOR origin;
	XMVECTOR direction;
	screenRay( camera, x, y, origin, direction );
	float distance = 0.0f;
	m_selected = models.pick( origin, direction, distance );
	m_selectedKey = models.key( m_selected );
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
