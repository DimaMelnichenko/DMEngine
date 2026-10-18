// Звук без устройства: микшер (сглаживание, затухание убранного голоса, ограничитель), стерео, синтез, фразы записей,
// чтение WAV, прослушивание в редакторе (Mute и Solo частей) и правила слоёв певцов
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include "Engine\Audio\AudioAudition.h"
#include "Engine\Audio\AudioClip.h"
#include "Engine\Audio\AudioMixer.h"
#include "Engine\Audio\AudioSynth.h"
#include "Engine\Audio\SoundRules.h"

using namespace GS;
using namespace DirectX;

namespace
{

// Генератор постоянного уровня — видно, что делает с ним микшер
class ConstantGenerator : public AudioGenerator
{
public:
	explicit ConstantGenerator( float value ) : m_value( value ) {}
	void render( float* out, uint32_t frames, const float* ) override { std::fill( out, out + frames, m_value ); }

private:
	float m_value;
};

constexpr uint32_t block = 480;

// Смесь за seconds секунд, каналы через один
std::vector<float> mix( AudioMixer& mixer, float seconds )
{
	std::vector<float> result;
	std::vector<float> buffer( block * AudioMixer::channels );
	for( uint32_t done = 0; done < seconds * AudioMixer::sampleRate; done += block )
	{
		mixer.render( buffer.data(), block );
		result.insert( result.end(), buffer.begin(), buffer.end() );
	}
	return result;
}

float largestStep( const std::vector<float>& stereo )
{
	float step = 0.0f;
	for( size_t i = 2; i < stereo.size(); ++i )
		step = std::max( step, std::abs( stereo[i] - stereo[i - 2] ) );
	return step;
}

float rms( const std::vector<float>& samples )
{
	double sum = 0.0;
	for( float sample : samples )
		sum += static_cast<double>( sample ) * sample;
	return static_cast<float>( std::sqrt( sum / std::max<size_t>( samples.size(), 1 ) ) );
}

AudioVoiceTarget centre( float gain )
{
	AudioVoiceTarget target;
	target.left = gain;
	target.right = gain;
	return target;
}

// WAV: 16 бит или float32, каналы через один
void writeWav( const std::filesystem::path& path, const std::vector<float>& samples, uint16_t channels, uint32_t rate, bool floatSamples )
{
	std::ofstream file( path, std::ios::binary );
	const uint16_t bits = floatSamples ? 32 : 16;
	const uint32_t dataBytes = static_cast<uint32_t>( samples.size() * bits / 8 );
	auto u32 = [&]( uint32_t v ) { file.write( reinterpret_cast<const char*>( &v ), 4 ); };
	auto u16 = [&]( uint16_t v ) { file.write( reinterpret_cast<const char*>( &v ), 2 ); };
	file.write( "RIFF", 4 );
	u32( 36 + dataBytes );
	file.write( "WAVEfmt ", 8 );
	u32( 16 );
	u16( floatSamples ? 3 : 1 );
	u16( channels );
	u32( rate );
	u32( rate * channels * bits / 8 );
	u16( static_cast<uint16_t>( channels * bits / 8 ) );
	u16( bits );
	file.write( "data", 4 );
	u32( dataBytes );
	for( float sample : samples )
	{
		if( floatSamples )
			file.write( reinterpret_cast<const char*>( &sample ), 4 );
		else
		{
			const int16_t value = static_cast<int16_t>( std::lround( sample * 32767.0f ) );
			file.write( reinterpret_cast<const char*>( &value ), 2 );
		}
	}
}

}

TEST_CASE( "AudioMixer: a voice comes in smoothly and reaches its target" )
{
	AudioMixer mixer;
	const AudioMixer::VoiceId voice = mixer.addVoice( std::make_unique<ConstantGenerator>( 1.0f ) );
	mixer.setTarget( voice, centre( 0.5f ) );
	const std::vector<float> stereo = mix( mixer, 0.5f );
	CHECK( std::abs( stereo[0] ) < 0.05f );						// не сразу на полную
	CHECK( stereo[stereo.size() - 2] == doctest::Approx( 0.5f ).epsilon( 0.01 ) );
	CHECK( largestStep( stereo ) < 0.01f );						// без скачков — без щелчков
	CHECK( mixer.stats().voices == 1 );
}

TEST_CASE( "AudioMixer: a removed voice fades out and is dropped" )
{
	AudioMixer mixer;
	const AudioMixer::VoiceId voice = mixer.addVoice( std::make_unique<ConstantGenerator>( 1.0f ) );
	mixer.setTarget( voice, centre( 0.5f ) );
	mix( mixer, 0.5f );
	mixer.removeVoice( voice );
	const std::vector<float> stereo = mix( mixer, 1.0f );
	CHECK( largestStep( stereo ) < 0.01f );
	CHECK( std::abs( stereo.back() ) < 1e-4f );
	CHECK( mixer.stats().voices == 0 );
}

TEST_CASE( "AudioMixer: the limiter keeps loud mixes below full scale" )
{
	AudioMixer mixer;
	for( int i = 0; i < 3; ++i )
		mixer.setTarget( mixer.addVoice( std::make_unique<ConstantGenerator>( 1.0f ) ), centre( 1.0f ) );
	const std::vector<float> stereo = mix( mixer, 0.5f );
	float peak = 0.0f;
	for( float sample : stereo )
		peak = std::max( peak, std::abs( sample ) );
	CHECK( peak <= AudioMixer::limiterThreshold + 1e-3f );
	CHECK( mixer.stats().clipped == 0 );
	CHECK( mixer.stats().limited > 0 );
}

TEST_CASE( "spatialize: equal power stereo by azimuth, behind is muffled, spread centres" )
{
	AudioListener listener;		// смотрит на +Z, вправо — +X
	AudioVoiceTarget right, left, behind, front, around;
	spatialize( listener, XMFLOAT3( 1.0f, 0.0f, 0.0f ), 1.0f, right );
	spatialize( listener, XMFLOAT3( -1.0f, 0.0f, 0.0f ), 1.0f, left );
	spatialize( listener, XMFLOAT3( 0.0f, 0.0f, -1.0f ), 1.0f, behind );
	spatialize( listener, XMFLOAT3( 0.0f, 0.0f, 1.0f ), 1.0f, front );
	spatialize( listener, XMFLOAT3( 1.0f, 0.0f, 0.0f ), 1.0f, around, 1.0f );
	CHECK( right.right > 0.99f );
	CHECK( right.left < 0.01f );
	CHECK( left.left > 0.99f );
	CHECK( front.left == doctest::Approx( front.right ) );
	CHECK( front.left * front.left + front.right * front.right == doctest::Approx( 1.0f ) );	// равная мощность
	CHECK( behind.lowpass < front.lowpass );
	CHECK( behind.left < front.left );
	CHECK( around.left == doctest::Approx( around.right ) );
}

TEST_CASE( "Synth generators produce finite sound" )
{
	std::vector<float> samples( 4800 );
	const float wind[AudioGenerator::controlCount] = { 6.0f, 0.5f, 120.0f };
	WindGenerator windGenerator( 1 );
	for( int i = 0; i < 10; ++i )
		windGenerator.render( samples.data(), static_cast<uint32_t>( samples.size() ), wind );
	CHECK( rms( samples ) > 1e-4f );
	CHECK( std::all_of( samples.begin(), samples.end(), []( float s ) { return std::isfinite( s ); } ) );

	const float brook[AudioGenerator::controlCount] = { 1.0f, 0.5f, 0.0f };
	BrookGenerator brookGenerator( 2 );
	for( int i = 0; i < 10; ++i )
		brookGenerator.render( samples.data(), static_cast<uint32_t>( samples.size() ), brook );
	CHECK( rms( samples ) > 1e-4f );
	CHECK( std::all_of( samples.begin(), samples.end(), []( float s ) { return std::isfinite( s ); } ) );
}

TEST_CASE( "ClipGenerator: a phrase lasts by its pitch and its edges are silent" )
{
	auto clip = std::make_shared<AudioClip>();
	clip->samples.assign( AudioMixer::sampleRate, 1.0f );
	const AudioClip::Phrase phrase{ 0, AudioMixer::sampleRate };
	CHECK( ClipGenerator( clip, phrase, 1.0f ).seconds() == doctest::Approx( 1.0f ) );
	CHECK( ClipGenerator( clip, phrase, 2.0f ).seconds() == doctest::Approx( 0.5f ) );

	ClipGenerator generator( clip, phrase, 1.0f );
	std::vector<float> samples( AudioMixer::sampleRate + 4800 );
	generator.render( samples.data(), static_cast<uint32_t>( samples.size() ), nullptr );
	CHECK( samples[0] < 0.01f );												// вход — с нуля
	CHECK( samples[AudioMixer::sampleRate / 2] == doctest::Approx( 1.0f ) );
	CHECK( samples[AudioMixer::sampleRate - 2] < 0.01f );						// выход — к нулю
	CHECK( samples.back() == 0.0f );											// дозвучала — тишина
}

TEST_CASE( "AudioClip: WAV 16-bit and float, stereo to mono, phrases" )
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path();
	const std::filesystem::path pcm = folder / "dmengine_test_pcm16.wav";
	const std::filesystem::path flt = folder / "dmengine_test_float.wav";
	const std::filesystem::path slow = folder / "dmengine_test_44100.wav";
	std::vector<float> stereo;
	for( uint32_t i = 0; i < AudioMixer::sampleRate; ++i )
	{
		stereo.push_back( 0.5f );
		stereo.push_back( 0.25f );
	}
	writeWav( pcm, stereo, 2, AudioMixer::sampleRate, false );
	writeWav( flt, stereo, 2, AudioMixer::sampleRate, true );
	writeWav( slow, stereo, 2, 44100, false );

	std::string reason;
	auto clip = AudioClip::load( pcm.string(), "0.1-0.2;bad;0.5-0.4;0.3-0.35", reason );
	REQUIRE( clip );
	CHECK( clip->samples.size() == AudioMixer::sampleRate );
	CHECK( clip->samples[100] == doctest::Approx( 0.375f ).epsilon( 0.001 ) );		// каналы — в один
	REQUIRE( clip->phrases.size() == 2 );											// неверные фразы — пропущены
	CHECK( clip->phrases[0].start == AudioMixer::sampleRate / 10 );
	CHECK( std::abs( static_cast<int>( clip->phrases[1].end ) - static_cast<int>( 0.35 * AudioMixer::sampleRate + 0.5 ) ) <= 1 );

	auto floatClip = AudioClip::load( flt.string(), "", reason );
	REQUIRE( floatClip );
	CHECK( floatClip->samples[100] == doctest::Approx( 0.375f ) );
	REQUIRE( floatClip->phrases.size() == 1 );										// без фраз — вся запись
	CHECK( floatClip->phrases[0].end == AudioMixer::sampleRate );

	CHECK_FALSE( AudioClip::load( slow.string(), "", reason ) );					// не 48 кГц — не читается
	CHECK( reason.find( "44100" ) != std::string::npos );
	CHECK_FALSE( AudioClip::load( ( folder / "dmengine_no_such_file.wav" ).string(), "", reason ) );

	std::filesystem::remove( pcm );
	std::filesystem::remove( flt );
	std::filesystem::remove( slow );
}

TEST_CASE( "AudioAudition: Mute silences, Solo leaves only the soloed parts" )
{
	AudioAudition audition;
	audition.beginFrame();
	CHECK( audition.gain( false, false ) == 1.0f );
	CHECK( audition.gain( true, false ) == 0.0f );		// Mute
	CHECK_FALSE( audition.anySolo() );

	// Solo отмечен в кадре — в силе со следующего: части разных объектов друг о друге не знают
	CHECK( audition.gain( false, true ) == 1.0f );
	CHECK( audition.gain( false, false ) == 1.0f );
	audition.beginFrame();
	CHECK( audition.anySolo() );
	CHECK( audition.gain( false, true ) == 1.0f );		// с Solo — звучит
	CHECK( audition.gain( false, false ) == 0.0f );		// без — молчит
	CHECK( audition.gain( true, true ) == 0.0f );		// Mute сильнее Solo

	// Solo сняли — со следующего кадра снова звучат все
	audition.beginFrame();
	CHECK( audition.anySolo() );
	CHECK( audition.gain( false, false ) == 0.0f );
	audition.beginFrame();
	CHECK_FALSE( audition.anySolo() );
	CHECK( audition.gain( false, false ) == 1.0f );

	// «Clear solo» — новый номер сброса
	const uint32_t seen = audition.clearRevision();
	audition.clearSolo();
	CHECK( audition.clearRevision() != seen );
}

TEST_CASE( "SoundRules: hours window, product of rules, values as text" )
{
	// Окно часов: внутри — 1, снаружи — 0, на границе — половина, край — за fade часов; через полночь
	CHECK( hoursActivity( 4.0f, 22.0f, 0.75f, 12.0f ) == doctest::Approx( 1.0f ) );
	CHECK( hoursActivity( 4.0f, 22.0f, 0.75f, 2.0f ) == doctest::Approx( 0.0f ) );
	CHECK( hoursActivity( 4.0f, 22.0f, 0.75f, 4.0f ) == doctest::Approx( 0.5f ) );
	CHECK( hoursActivity( 4.0f, 22.0f, 0.75f, 22.375f ) == doctest::Approx( 0.0f ) );
	CHECK( hoursActivity( 21.0f, 5.0f, 0.75f, 23.5f ) == doctest::Approx( 1.0f ) );
	CHECK( hoursActivity( 21.0f, 5.0f, 0.75f, 2.0f ) == doctest::Approx( 1.0f ) );
	CHECK( hoursActivity( 21.0f, 5.0f, 0.75f, 12.0f ) == doctest::Approx( 0.0f ) );

	const SoundRuleType* hours = soundRuleType( "hours" );
	REQUIRE( hours );
	CHECK_FALSE( soundRuleType( "no_such_rule" ) );

	// Текст параметров: нет ключа — по умолчанию, лишний и не число — пропуск; обратно — по порядку параметров
	const std::vector<float> values = parseSoundRuleValues( *hours, " to = 18 ;from=6;extra=1;fade=x" );
	REQUIRE( values.size() == hours->params.size() );
	CHECK( values[0] == doctest::Approx( 6.0f ) );
	CHECK( values[1] == doctest::Approx( 18.0f ) );
	CHECK( values[2] == doctest::Approx( hours->params[2].defaultValue ) );
	CHECK( formatSoundRuleValues( *hours, values ) == "from=6;to=18;fade=0.75" );
	CHECK( parseSoundRuleValues( *hours, formatSoundRuleValues( *hours, values ) ) == values );

	// Активность — произведение включённых правил; выключенное и неизвестное — 1; правил нет — 1
	SoundRuleContext context;
	context.hour = 7.0f;
	SoundRule day{ 0, "hours", true, parseSoundRuleValues( *hours, "from=6;to=18" ) };
	SoundRule morning{ 0, "hours", true, parseSoundRuleValues( *hours, "from=5;to=9" ) };
	SoundRule evening{ 0, "hours", true, parseSoundRuleValues( *hours, "from=17;to=21" ) };
	SoundRule unknown{ 0, "no_such_rule", true, {} };
	CHECK( soundActivity( {}, context ) == doctest::Approx( 1.0f ) );
	CHECK( soundActivity( { day, morning }, context ) == doctest::Approx( 1.0f ) );
	CHECK( soundActivity( { day, evening }, context ) == doctest::Approx( 0.0f ) );
	evening.enabled = false;
	CHECK( soundActivity( { day, evening, unknown }, context ) == doctest::Approx( 1.0f ) );
	context.hour = 6.0f;
	CHECK( soundActivity( { day, morning }, context ) == doctest::Approx( 0.5f ) );
}
