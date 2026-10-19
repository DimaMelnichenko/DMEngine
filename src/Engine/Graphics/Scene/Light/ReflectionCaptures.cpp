#include "ReflectionCaptures.h"
#include <algorithm>
#include <cmath>
#include "ConstantBuffers.h"
#include "SceneTargets.h"
#include "Shaders\slots.h"
#include "Shaders\reflection_capture_constants.h"
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"
#include "Properties\SettingsProperties.h"

using namespace DirectX;

namespace GS
{

namespace
{

// Грани кубической карты по соглашению Direct3D (Shaders/cubemap.sh): +X, −X, +Y, −Y, +Z, −Z — направление взгляда и верх
// камеры грани (вправо — cross( up, forward ), v вниз)
const XMFLOAT3 faceForward[6] = { { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
								  { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } };
const XMFLOAT3 faceUp[6] = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f },
							 { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };

float distanceSquared( const XMFLOAT3& a, const XMFLOAT3& b )
{
	const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
	return x * x + y * y + z * z;
}

}

ReflectionCaptures::ReflectionCaptures() : SceneObject( "Reflection captures" )
{
	m_properties.setName( "Reflection captures" );
}

bool ReflectionCaptures::initialize( const std::vector<Settings>& captures )
{
	if( !m_prefilter.initialize( SceneTargets::colorFormat ) ||
		!m_coverageShader.initialize( "Shaders\\reflection_capture_coverage.cs", "main" ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( ReflectionCaptureCoverageBuffer ), m_coverageConstants ) )
		return false;
	for( const Settings& settings : captures )
	{
		auto capture = std::make_unique<Capture>();
		capture->settings = settings;
		if( !createResources( *capture ) )
			return false;
		// Окно: подокно на карту
		PropertyContainer& properties = capture->properties;
		properties.setName( settings.name );
		addSettingProperties( properties, reflectionCaptureSchema(), settings, "" );
		Capture* target = capture.get();
		properties.addButton( "Recapture", "Capture the scene into the map again (after an edit of the terrain, forest or models)",
							  [target]
		{
			restart( *target );
			return PropertyContainer::ButtonAction{};
		} );
		capture->appliedRevision = properties.revision();
		m_properties.addSubContainer( &properties );
		m_captures.push_back( std::move( capture ) );
	}
	return true;
}

bool ReflectionCaptures::createResources( Capture& capture )
{
	// Цели съёмки и префильтр — в формате буфера сцены (HDR, яркость × экспозиция съёмки; съёмка без неба — очистка
	// прозрачным чёрным); глубина — одна на все грани, после грани по ней покрытие
	const uint32_t size = static_cast<uint32_t>( std::max( capture.settings.faceSize, 16 ) );
	TextureDesc depthDesc;
	depthDesc.width = size;
	depthDesc.height = size;
	depthDesc.format = SceneTargets::depthTextureFormat;
	depthDesc.usage = TextureUsage::depthStencil | TextureUsage::shaderResource;
	TextureViewDesc depthViewDesc;
	depthViewDesc.format = SceneTargets::depthViewFormat;
	DMD3D& d3d = DMD3D::instance();
	if( !capture.source.create( size, 0, SceneTargets::colorFormat, true, "Reflection capture source",
								SceneCapture::transparentClearColor ) ||
		!capture.specular.create( size, mipCount, SceneTargets::colorFormat, false, "Reflection capture" ) ||
		!capture.distance[0].create( size, 1, DXGI_FORMAT_R32_FLOAT, true, "Reflection capture distance" ) ||
		!capture.distance[1].create( size, 1, DXGI_FORMAT_R32_FLOAT, true, "Reflection capture distance" ) ||
		!d3d.createTexture( depthDesc, nullptr, capture.depth ) || !d3d.createTargetView( capture.depth, {}, capture.depthTarget ) ||
		!d3d.createShaderView( capture.depth, depthViewDesc, capture.depthView ) )
	{
		LOG( "Reflection capture " + capture.settings.name + ": failed to create the cube " + std::to_string( size ) );
		return false;
	}
	d3d.setName( capture.depth, "Reflection capture depth" );
	// Новая карта ещё не снята
	capture.ready = false;
	restart( capture );
	return true;
}

ReflectionCaptures::Settings ReflectionCaptures::current( const Capture& capture ) const
{
	Settings settings = capture.settings;
	readSettingProperties( capture.properties, reflectionCaptureSchema(), settings );
	return settings;
}

void ReflectionCaptures::restart( Capture& capture )
{
	capture.nextFace = 0;
	capture.prefilterPending = false;
}

std::vector<ReflectionCaptures::Settings> ReflectionCaptures::captureSettings() const
{
	std::vector<Settings> result;
	for( const auto& capture : m_captures )
		result.push_back( current( *capture ) );
	return result;
}

void ReflectionCaptures::update( const FrameContext& frame )
{
	const float recaptureCos = std::cos( XMConvertToRadians( recaptureLightAngle ) );
	for( const auto& pointer : m_captures )
	{
		Capture& capture = *pointer;
		// Правка окна закончена: точка или размер грани — съёмка заново (новый размер — новые цели); радиус, переход и
		// яркость берёт frameConstants каждый кадр
		if( !capture.properties.editing() && capture.properties.revision() != capture.appliedRevision )
		{
			capture.appliedRevision = capture.properties.revision();
			const Settings settings = current( capture );
			const bool resized = settings.faceSize != capture.settings.faceSize;
			const bool moved = distanceSquared( settings.position, capture.settings.position ) > 0.0f;
			capture.settings = settings;
			if( resized && !createResources( capture ) )
				LOG( "Reflection capture " + capture.settings.name + " is not recreated" );
			if( moved )
				restart( capture );
		}
		// Светило теней сдвинулось (время суток) или сменилось (ночью — луна): в карте небо, свет и тени прежнего времени
		const XMFLOAT3& light = frame.toShadowLight;
		const XMFLOAT3& captured = capture.toLight;
		if( capture.nextFace >= faceCount && !capture.prefilterPending &&
			light.x * captured.x + light.y * captured.y + light.z * captured.z < recaptureCos )
			restart( capture );
		// Экспозиция кадра ушла от экспозиции съёмки (смена дня и ночи): точности half-float карты на новую не хватает
		const float low = std::max( std::min( frame.exposure, capture.shownExposure ), 1e-30f );
		const float high = std::max( frame.exposure, capture.shownExposure );
		if( capture.ready && capture.nextFace >= faceCount && !capture.prefilterPending && frame.exposure > 0.0f &&
			high > low * recaptureExposureRatio )
			restart( capture );
	}
}

bool ReflectionCaptures::requestCapture( const FrameContext& frame, SceneCapture& capture )
{
	for( const auto& pointer : m_captures )
	{
		Capture& target = *pointer;
		if( target.nextFace >= faceCount )
			continue;
		const int32_t face = target.nextFace;
		if( face == 0 )
			target.toLight = frame.toShadowLight;
		// Грань — квадратная пирамида 90°; плоскости — главной камеры (горы вокруг карты — до 100 км)
		const uint32_t size = target.source.size();
		capture.view = RenderView::perspective( target.settings.position, faceForward[face], faceUp[face], XM_PIDIV2, 1.0f,
												frame.view.nearPlane, frame.view.farPlane );
		// Без неба: в отражении оно — по направлению луча (освещение окружением), а диски светил дал бы блик их света
		capture.sky = false;
		capture.color = &target.source.target( 0, static_cast<uint32_t>( face ) );
		capture.depth = &target.depthTarget;
		capture.width = size;
		capture.height = size;
		// Все грани — с экспозицией первой (экспозиция главного вида при начале съёмки)
		capture.exposure = face == 0 ? 0.0f : target.exposure;
		Capture* owner = &target;
		capture.done = [this, owner, face]( const SceneCapture& done )
		{
			// Глубина у граней одна — покрытие и расстояния сразу, до следующей грани
			writeCoverage( *owner, face, done );
			if( face == 0 )
				owner->exposure = done.exposure;
			owner->nextFace = face + 1;
			owner->prefilterPending = owner->nextFace >= faceCount;
		};
		return true;
	}
	return false;
}

void ReflectionCaptures::writeCoverage( const Capture& capture, int32_t face, const SceneCapture& faceCapture )
{
	DMD3D& d3d = DMD3D::instance();
	const uint32_t size = capture.source.size();
	// z вида по обратной глубине грани: P₃₂ / (глубина − P₂₂), как reflectionViewDepth в шейдерах
	XMFLOAT4X4 projection;
	XMStoreFloat4x4( &projection, faceCapture.view.projection );
	ReflectionCaptureCoverageBuffer constants = {};
	constants.face = face;
	constants.size = size;
	constants.projection22 = projection._33;
	constants.projection32 = projection._43;
	Device::updateResourceData<ReflectionCaptureCoverageBuffer>( m_coverageConstants, constants );
	d3d.setConstantBuffer( SLOT_CB_PASS, m_coverageConstants );
	const CubeTarget& distance = capture.distance[1 - capture.shownDistance];
	d3d.beginPass( PassDesc{ "Reflection capture coverage", {}, {}, 0, 0, { { &capture.depthView, "capture depth" } },
							 { { &capture.source.mipStorage( 0 ), "capture cube" }, { &distance.mipStorage( 0 ), "capture distance" } } } );
	d3d.setSRV( 0, capture.depthView );
	m_coverageShader.setUAVBuffer( 0, capture.source.mipStorage( 0 ) );
	m_coverageShader.setUAVBuffer( 1, distance.mipStorage( 0 ) );
	const uint32_t groups = ( size + 7 ) / 8;
	m_coverageShader.dispatchGroups( groups, groups, 1 );
}

void ReflectionCaptures::frameConstants( const RenderView& view, FrameParameters& constants )
{
	// Две ближайшие к камере карты, у которых есть что показать (готова или префильтр — в compute этого кадра, до проходов)
	std::vector<std::pair<float, const Capture*>> shown;
	for( const auto& capture : m_captures )
		if( capture->ready || capture->prefilterPending )
			shown.emplace_back( distanceSquared( view.position, capture->settings.position ), capture.get() );
	std::sort( shown.begin(), shown.end(), []( const auto& a, const auto& b ) { return a.first < b.first; } );
	for( uint32_t i = 0; i < shownCount; ++i )
	{
		m_shown[i] = i < shown.size() ? shown[i].second : nullptr;
		if( !m_shown[i] )
			continue;
		const Capture& capture = *m_shown[i];
		const Settings settings = current( capture );
		const float exposure = capture.prefilterPending ? capture.exposure : capture.shownExposure;
		const float radius = std::max( settings.influenceRadius, 0.01f );
		constants.reflectionCaptureSphere[i] = XMFLOAT4( capture.settings.position.x, capture.settings.position.y,
														 capture.settings.position.z, radius );
		constants.reflectionCapture[i] = XMFLOAT4( settings.brightness / std::max( exposure, 1e-30f ),
												   1.0f / std::max( radius * settings.transition, 0.01f ), 0.0f, 0.0f );
	}
}

void ReflectionCaptures::compute( const FrameContext& )
{
	DMD3D& d3d = DMD3D::instance();
	for( const auto& pointer : m_captures )
	{
		Capture& capture = *pointer;
		if( !capture.prefilterPending )
			continue;
		// Все грани сняты: мипы источника — для выборки по важности, затем префильтр граней (результат — в единицах съёмки)
		m_prefilter.buildMips( capture.source );
		for( int32_t face = 0; face < faceCount; ++face )
			m_prefilter.prefilterFace( capture.source, capture.specular, face );
		capture.prefilterPending = false;
		capture.ready = true;
		capture.shownExposure = capture.exposure;
		// Расстояния новой съёмки — показанные
		capture.shownDistance = 1 - capture.shownDistance;
	}
	// Карты кадра — в слоты сцены; нет второй — её слот не читают (радиус 0 в константах)
	static constexpr uint16_t colorSlots[shownCount] = { SLOT_REFLECTION_CAPTURE_0, SLOT_REFLECTION_CAPTURE_1 };
	static constexpr uint16_t distanceSlots[shownCount] = { SLOT_REFLECTION_CAPTURE_DISTANCE_0, SLOT_REFLECTION_CAPTURE_DISTANCE_1 };
	for( uint32_t i = 0; i < shownCount; ++i )
	{
		if( !m_shown[i] )
			continue;
		d3d.setSRV( colorSlots[i], m_shown[i]->specular.srv() );
		d3d.setSRV( distanceSlots[i], m_shown[i]->distance[m_shown[i]->shownDistance].srv() );
	}
}

}
