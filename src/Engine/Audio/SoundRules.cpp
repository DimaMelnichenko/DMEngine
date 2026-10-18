#include "SoundRules.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace GS
{

namespace
{

float hoursRule( const float* values, const SoundRuleContext& context )
{
	return hoursActivity( values[0], values[1], values[2], context.hour );
}

}

const std::vector<SoundRuleType>& soundRuleTypes()
{
	// Новое правило — строка здесь: имя в базе, подпись, параметры (ключ, подпись, по умолчанию, границы, единицы, подсказка),
	// оценка. Дальше по плану (TODO.md, «Звук — дальше»): солнце (высота над горизонтом — рассвет и закат), сезон, ветер и
	// погода, кривая по часам (рассветный хор)
	static const std::vector<SoundRuleType> types = {
		{ "hours", "Hours", "Sings in these hours of the day (world clock); from > to - over midnight (crickets, owls)",
		  {
			  { "from", "From", 4.0f, 0.0f, 24.0f, "h", "Starts to sing" },
			  { "to", "To", 22.0f, 0.0f, 24.0f, "h", "Stops singing" },
			  { "fade", "Fade", 0.75f, 0.0f, 4.0f, "h", "The edges fade over this long: half the singers right at the edge" },
		  },
		  hoursRule },
	};
	return types;
}

const SoundRuleType* soundRuleType( const std::string& name )
{
	for( const SoundRuleType& type : soundRuleTypes() )
		if( name == type.name )
			return &type;
	return nullptr;
}

std::vector<float> parseSoundRuleValues( const SoundRuleType& type, const std::string& text )
{
	std::vector<float> values;
	for( const SoundRuleParam& param : type.params )
		values.push_back( param.defaultValue );
	size_t start = 0;
	while( start <= text.size() )
	{
		const size_t end = std::min( text.find( ';', start ), text.size() );
		const std::string pair = text.substr( start, end - start );
		const size_t equals = pair.find( '=' );
		if( equals != std::string::npos )
		{
			auto trim = []( std::string s )
			{
				s.erase( 0, s.find_first_not_of( " \t" ) );
				s.erase( s.find_last_not_of( " \t" ) + 1 );
				return s;
			};
			const std::string key = trim( pair.substr( 0, equals ) );
			const std::string number = trim( pair.substr( equals + 1 ) );
			char* parsed = nullptr;
			const float value = std::strtof( number.c_str(), &parsed );
			for( size_t i = 0; i < type.params.size(); ++i )
				if( key == type.params[i].key && !number.empty() && parsed && *parsed == '\0' )
					values[i] = value;
		}
		start = end + 1;
	}
	return values;
}

std::string formatSoundRuleValues( const SoundRuleType& type, const std::vector<float>& values )
{
	std::string text;
	for( size_t i = 0; i < type.params.size() && i < values.size(); ++i )
	{
		char number[32];
		std::snprintf( number, sizeof( number ), "%g", values[i] );
		text += ( i ? ";" : "" ) + std::string( type.params[i].key ) + "=" + number;
	}
	return text;
}

float soundRuleFactor( const SoundRule& rule, const SoundRuleContext& context )
{
	const SoundRuleType* type = soundRuleType( rule.type );
	if( !rule.enabled || !type || rule.values.size() < type->params.size() )
		return 1.0f;
	return std::clamp( type->evaluate( rule.values.data(), context ), 0.0f, 1.0f );
}

float soundActivity( const std::vector<SoundRule>& rules, const SoundRuleContext& context )
{
	float activity = 1.0f;
	for( const SoundRule& rule : rules )
		activity *= soundRuleFactor( rule, context );
	return activity;
}

float hoursActivity( float from, float to, float fade, float hour )
{
	// «От» больше «до» — через полночь: часы после полуночи — за 24
	const float edge = std::max( fade, 1e-3f );
	if( to < from )
	{
		to += 24.0f;
		if( hour < from - edge )
			hour += 24.0f;
	}
	const float rise = std::clamp( ( hour - from ) / edge + 0.5f, 0.0f, 1.0f );
	const float fall = std::clamp( ( to - hour ) / edge + 0.5f, 0.0f, 1.0f );
	return rise * fall;
}

}
