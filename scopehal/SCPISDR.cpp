/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

#include "scopehal.h"
#include "SCPISDR.h"
#include "TouchstoneParser.h"
#include <fstream>

using namespace std;

SCPISDR::SDRCreateMapType SCPISDR::m_sdrcreateprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPISDR::SCPISDR()
{
	m_serializers.push_back(sigc::mem_fun(*this, &SCPISDR::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(*this, &SCPISDR::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(*this, &SCPISDR::DoPreLoadConfiguration));
}

SCPISDR::~SCPISDR()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void SCPISDR::DoAddDriverClass(const string& name, SDRCreateProcType proc)
{
	m_sdrcreateprocs[name] = proc;
}

//This is intentionally not virtual since it's a static method used by enumeration
//cppcheck-suppress duplInheritedMember
void SCPISDR::EnumDrivers(vector<string>& names)
{
	for(auto it=m_sdrcreateprocs.begin(); it != m_sdrcreateprocs.end(); ++it)
		names.push_back(it->first);
}

shared_ptr<SCPISDR> SCPISDR::CreateSDR(const string& driver, SCPITransport* transport)
{
	if(m_sdrcreateprocs.find(driver) != m_sdrcreateprocs.end())
		return m_sdrcreateprocs[driver](transport);

	LogError("Invalid SDR driver name \"%s\"\n", driver.c_str());
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default stubs for Oscilloscope methods

bool SCPISDR::IsChannelEnabled(size_t /*i*/)
{
	return true;
}

void SCPISDR::EnableChannel(size_t /*i*/)
{
	//no-op
}

void SCPISDR::DisableChannel(size_t /*i*/)
{
	//no-op
}

OscilloscopeChannel::CouplingType SCPISDR::GetChannelCoupling(size_t /*i*/)
{
	//not electrical inputs
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void SCPISDR::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//no-op, coupling cannot be changed
}

vector<OscilloscopeChannel::CouplingType> SCPISDR::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_SYNTHETIC);
	return ret;
}

double SCPISDR::GetChannelAttenuation(size_t /*i*/)
{
	return 1;
}

void SCPISDR::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//no-op
}

unsigned int SCPISDR::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void SCPISDR::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	//no-op
}

bool SCPISDR::IsInterleaving()
{
	return false;
}

bool SCPISDR::SetInterleaving(bool /*combine*/)
{
	return false;
}


bool SCPISDR::HasFrequencyControls()
{
	return false;
}

bool SCPISDR::HasTimebaseControls()
{
	return false;
}

void SCPISDR::SetTriggerOffset(int64_t /*offset*/)
{
}

int64_t SCPISDR::GetTriggerOffset()
{
	return 0;
}

vector<uint64_t> SCPISDR::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> SCPISDR::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> SCPISDR::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> SCPISDR::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(1);
	return ret;
}

void SCPISDR::SetSampleRate(uint64_t /*rate*/)
{
}

uint64_t SCPISDR::GetSampleRate()
{
	return 1;
}

unsigned int SCPISDR::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t SCPISDR::GetInstrumentTypesForChannel(size_t i) const
{
	//Transmit paths aren't oscilloscope inputs
	if( (i < m_channels.size()) && (dynamic_cast<SDRTransmitChannel*>(m_channels[i]) != nullptr) )
		return Instrument::INST_RF_GEN;

	return Instrument::INST_OSCILLOSCOPE;
}

float SCPISDR::GetChannelVoltageRange(size_t i, size_t stream)
{
	//range in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRange[pair<size_t, size_t>(i, stream)];
}

void SCPISDR::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	//Range is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRange[pair<size_t, size_t>(i, stream)]= range;
}

float SCPISDR::GetChannelOffset(size_t i, size_t stream)
{
	//offset in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffset[pair<size_t, size_t>(i, stream)];
}

void SCPISDR::SetChannelOffset(size_t i, size_t stream, float offset)
{
	//Offset is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffset[pair<size_t, size_t>(i, stream)] = offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Gain control (default is no gain control)

bool SCPISDR::HasGainControl(size_t /*i*/)
{
	return false;
}

vector<string> SCPISDR::GetGainModes(size_t /*i*/)
{
	return vector<string>();
}

string SCPISDR::GetGainMode(size_t /*i*/)
{
	return "";
}

void SCPISDR::SetGainMode(size_t /*i*/, const string& /*mode*/)
{
	//no-op
}

bool SCPISDR::IsGainAdjustable(size_t i)
{
	return HasGainControl(i);
}

pair<float, float> SCPISDR::GetGainRange(size_t /*i*/)
{
	return pair<float, float>(0, 0);
}

float SCPISDR::GetGain(size_t /*i*/)
{
	return 0;
}

void SCPISDR::SetGain(size_t /*i*/, float /*gain*/)
{
	//no-op
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Level correction (default is no correction)

bool SCPISDR::HasLevelCorrection(size_t /*i*/)
{
	return false;
}

float SCPISDR::GetExternalGain(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_levelCorrection[i].m_externalGain;
}

void SCPISDR::SetExternalGain(size_t i, float gain)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	auto& lc = m_levelCorrection[i];
	float scale = pow(10, (lc.m_externalGain - gain) / 20);
	lc.m_externalGain = gain;

	//Scale the I/Q streams the same way the samples will be, so the waveform stays where it was on screen
	auto chan = GetOscilloscopeChannel(i);
	if(!chan)
		return;
	for(size_t j=0; j<chan->GetStreamCount(); j++)
	{
		if(chan->GetYAxisUnits(j) != Unit(Unit::UNIT_VOLTS))
			continue;
		auto key = pair<size_t, size_t>(i, j);
		m_channelVoltageRange[key] *= scale;
		m_channelOffset[key] *= scale;
	}
}

string SCPISDR::GetCalibrationFile(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_levelCorrection[i].m_calFile;
}

string SCPISDR::GetCalibrationError(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_levelCorrection[i].m_calError;
}

/**
	@brief Parses a frequency in Hz, optionally followed by an SI prefix and/or "Hz" (for example "2.4e9", "2400 MHz",
	or "2.4G")

	@return True if the whole string was a frequency
 */
static bool ParseCalFrequency(const string& str, double& freq)
{
	const char* start = str.c_str();
	char* end = nullptr;
	freq = strtod(start, &end);
	if(end == start)
		return false;

	while(isspace(static_cast<unsigned char>(*end)))
		end++;
	switch(*end)
	{
		case 'k':
		case 'K':
			freq *= 1e3;
			end++;
			break;

		case 'M':
			freq *= 1e6;
			end++;
			break;

		case 'G':
			freq *= 1e9;
			end++;
			break;

		case 'T':
			freq *= 1e12;
			end++;
			break;

		default:
			break;
	}
	if( (tolower(end[0]) == 'h') && (tolower(end[1]) == 'z') )
		end += 2;
	while(isspace(static_cast<unsigned char>(*end)))
		end++;

	return (*end == '\0') && isfinite(freq);
}

/**
	@brief Parses a gain in dB, optionally followed by "dB"

	@return True if the whole string was a gain
 */
static bool ParseCalGain(const string& str, float& gain)
{
	const char* start = str.c_str();
	char* end = nullptr;
	gain = strtof(start, &end);
	if(end == start)
		return false;

	while(isspace(static_cast<unsigned char>(*end)))
		end++;
	if( (tolower(end[0]) == 'd') && (tolower(end[1]) == 'b') )
		end += 2;
	while(isspace(static_cast<unsigned char>(*end)))
		end++;

	return (*end == '\0') && isfinite(gain);
}

/**
	@brief Loads a text calibration file, one "frequency, gain" point per line

	@return An empty string if it worked, otherwise why it didn't
 */
static string LoadTextCalibration(const string& path, vector<pair<double, float> >& points)
{
	ifstream in(path);
	if(!in)
		return "Could not open the file";

	string line;
	size_t lineNum = 0;
	while(getline(in, line))
	{
		lineNum ++;

		//Throw away comments and blank lines
		auto hash = line.find('#');
		if(hash != string::npos)
			line.resize(hash);
		auto first = line.find_first_not_of(" \t\r");
		if(first == string::npos)
			continue;
		line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);

		//Split into fields, using whitespace only if there's no other separator (so "2.4 GHz, 3" works)
		vector<string> fields;
		bool whitespace = (line.find_first_of(",;\t") == string::npos);
		string field;
		for(auto c : line)
		{
			bool sep = whitespace ? (c == ' ') : ( (c == ',') || (c == ';') || (c == '\t') );
			if(!sep)
				field += c;
			else if(!field.empty() || !whitespace)
			{
				fields.push_back(field);
				field.clear();
			}
		}
		fields.push_back(field);

		double freq;
		float gain;
		bool ok = (fields.size() == 2) && ParseCalFrequency(fields[0], freq) && ParseCalGain(fields[1], gain);
		if(!ok)
		{
			//A header line is allowed before the first point
			char c = line[0];
			if(points.empty() && !isdigit(static_cast<unsigned char>(c)) && (c != '.') && (c != '-') && (c != '+'))
				continue;
			return "Line " + to_string(lineNum) + " is not a frequency and a gain: \"" + line + "\"";
		}
		points.push_back(pair<double, float>(freq, gain));
	}

	return "";
}

/**
	@brief Loads a Touchstone calibration file, using S21 as the gain

	@return An empty string if it worked, otherwise why it didn't
 */
static string LoadTouchstoneCalibration(const string& path, vector<pair<double, float> >& points)
{
	SParameters params;
	TouchstoneParser parser;
	if(!parser.Load(path, params))
		return "Could not load the Touchstone file (see the log for details)";
	if(params.GetNumPorts() < 2)
		return "The Touchstone file has only one port, it needs at least two for S21";

	auto& s21 = params[SPair(2, 1)];
	s21.m_points.PrepareForCpuAccess();
	for(size_t j=0; j<s21.size(); j++)
	{
		auto& p = s21[j];
		points.push_back(pair<double, float>(p.m_frequency, 20 * log10(p.m_amplitude)));
	}
	return "";
}

bool SCPISDR::SetCalibrationFile(size_t i, const string& path)
{
	vector<pair<double, float> > points;
	string err;
	if(!path.empty())
	{
		auto dot = path.rfind('.');
		string ext = (dot == string::npos) ? "" : path.substr(dot);
		for(auto& c : ext)
			c = tolower(c);

		//Touchstone files are .sNp, for any number of ports N
		bool touchstone = (ext.size() >= 4) && (ext[1] == 's') && (ext.back() == 'p') &&
			(ext.find_first_not_of("0123456789", 2) == ext.size() - 1);
		err = touchstone ? LoadTouchstoneCalibration(path, points) : LoadTextCalibration(path, points);

		if(err.empty())
		{
			for(auto& p : points)
			{
				if(!isfinite(p.second))
				{
					err = "The gain at " + Unit(Unit::UNIT_HZ).PrettyPrint(p.first) + " is not a number";
					break;
				}
			}
		}
		if(err.empty() && points.empty())
			err = "The file has no calibration points in it";

		if(!err.empty())
		{
			LogWarning("Could not load SDR calibration file %s: %s\n", path.c_str(), err.c_str());
			points.clear();
		}
	}
	sort(points.begin(), points.end());

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	auto& lc = m_levelCorrection[i];
	lc.m_calFile = path;
	lc.m_calError = err;
	lc.m_calPoints = points;
	return err.empty();
}

float SCPISDR::GetCalibrationGain(size_t i, int64_t freq)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	auto& points = m_levelCorrection[i].m_calPoints;
	if(points.empty())
		return 0;

	//Flat beyond the ends
	double f = freq;
	if(f <= points.front().first)
		return points.front().second;
	if(f >= points.back().first)
		return points.back().second;

	//Linear interpolation between the points on either side
	auto hi = lower_bound(points.begin(), points.end(), pair<double, float>(f, -INFINITY));
	auto lo = hi - 1;
	double frac = (f - lo->first) / (hi->first - lo->first);
	return lo->second + frac * (hi->second - lo->second);
}

float SCPISDR::GetInputGain(size_t i, int64_t freq, float rxGain)
{
	return rxGain + GetExternalGain(i) + GetCalibrationGain(i, freq);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Received signal strength (default is no RSSI)

bool SCPISDR::HasRSSI(size_t /*i*/)
{
	return false;
}

bool SCPISDR::IsRSSIEnabled(size_t /*i*/)
{
	return false;
}

void SCPISDR::SetRSSIEnabled(size_t /*i*/, bool /*enable*/)
{
	//no-op
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sweeping (default is no sweep support)

bool SCPISDR::CanSweep()
{
	return false;
}

bool SCPISDR::IsSweepEnabled()
{
	return false;
}

void SCPISDR::SetSweepEnabled(bool /*enable*/)
{
	//no-op
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Transmit control (default is no transmitter)

size_t SCPISDR::GetTxChannelCount()
{
	return 0;
}

size_t SCPISDR::GetTxToneCount(size_t /*tx*/)
{
	return 0;
}

int64_t SCPISDR::GetTxLOFrequency()
{
	return 0;
}

void SCPISDR::SetTxLOFrequency(int64_t /*freq*/)
{
	//no-op
}

pair<int64_t, int64_t> SCPISDR::GetTxLOFrequencyRange()
{
	return pair<int64_t, int64_t>(0, 0);
}

int64_t SCPISDR::GetTxToneFrequency(size_t /*tx*/, size_t /*tone*/)
{
	return 0;
}

void SCPISDR::SetTxToneFrequency(size_t /*tx*/, size_t /*tone*/, int64_t /*freq*/)
{
	//no-op
}

pair<int64_t, int64_t> SCPISDR::GetTxToneFrequencyRange(size_t /*tx*/)
{
	return pair<int64_t, int64_t>(0, 0);
}

float SCPISDR::GetTxAttenuation(size_t /*tx*/)
{
	return 0;
}

void SCPISDR::SetTxAttenuation(size_t /*tx*/, float /*atten*/)
{
	//no-op
}

pair<float, float> SCPISDR::GetTxAttenuationRange(size_t /*tx*/)
{
	return pair<float, float>(0, 0);
}

float SCPISDR::GetTxToneAmplitude(size_t /*tx*/, size_t /*tone*/)
{
	return 0;
}

void SCPISDR::SetTxToneAmplitude(size_t /*tx*/, size_t /*tone*/, float /*amplitude*/)
{
	//no-op
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

//TODO Implement SCPISDR serialization
//This is called by Instrument::m_serializers and is not virtual
//cppcheck-suppress duplInheritedMember
void SCPISDR::DoSerializeConfiguration(YAML::Node& node, IDTable& /*table*/)
{
	if(CanSweep())
		node["sweep"] = IsSweepEnabled();

	//Transmitter (not tied to channel nodes since transmit paths aren't oscilloscope channels)
	size_t ntx = GetTxChannelCount();
	if(ntx > 0)
	{
		YAML::Node tx;
		tx["lo"] = GetTxLOFrequency();
		for(size_t i=0; i<ntx; i++)
		{
			YAML::Node txnode;
			txnode["attenuation"] = GetTxAttenuation(i);
			for(size_t j=0; j<GetTxToneCount(i); j++)
			{
				YAML::Node tone;
				tone["freq"] = GetTxToneFrequency(i, j);
				tone["amplitude"] = GetTxToneAmplitude(i, j);
				txnode["tone" + to_string(j)] = tone;
			}
			tx["tx" + to_string(i)] = txnode;
		}
		node["tx"] = tx;
	}

	//Channel nodes themselves are created by Oscilloscope, we just add the SDR specific settings
	YAML::Node channels = node["channels"];
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		bool hasGain = HasGainControl(i);
		bool hasRSSI = HasRSSI(i);
		bool hasLevel = HasLevelCorrection(i);
		if(!hasGain && !hasRSSI && !hasLevel)
			continue;

		YAML::Node channelNode = channels["ch" + to_string(i)];
		channelNode["index"] = i;
		if(hasGain)
		{
			auto mode = GetGainMode(i);
			if(!mode.empty())
				channelNode["gainmode"] = mode;
			channelNode["gain"] = GetGain(i);
		}
		if(hasRSSI)
			channelNode["rssi"] = IsRSSIEnabled(i);
		if(hasLevel)
		{
			channelNode["externalgain"] = GetExternalGain(i);
			channelNode["calfile"] = GetCalibrationFile(i);
		}
	}
}

//This is called by Instrument::m_preloaders and is not virtual
//cppcheck-suppress duplInheritedMember
void SCPISDR::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& /*idmap*/)
{
	//Sweeping limits the span, so it has to be set first. Oscilloscope has already set the span, but it may have been
	//cut down to one capture if sweeping wasn't enabled yet, so set it again.
	if(CanSweep() && node["sweep"])
		SetSweepEnabled(node["sweep"].as<bool>());
	if(HasFrequencyControls() && node["span"])
		SetSpan(node["span"].as<int64_t>());

	//Transmitter
	auto tx = node["tx"];
	size_t ntx = GetTxChannelCount();
	if(tx && (ntx > 0))
	{
		if(tx["lo"])
			SetTxLOFrequency(tx["lo"].as<int64_t>());

		for(size_t i=0; i<ntx; i++)
		{
			auto txnode = tx["tx" + to_string(i)];
			if(!txnode)
				continue;

			if(txnode["attenuation"])
				SetTxAttenuation(i, txnode["attenuation"].as<float>());

			for(size_t j=0; j<GetTxToneCount(i); j++)
			{
				auto tone = txnode["tone" + to_string(j)];
				if(!tone)
					continue;

				if(tone["freq"])
					SetTxToneFrequency(i, j, tone["freq"].as<int64_t>());
				if(tone["amplitude"])
					SetTxToneAmplitude(i, j, tone["amplitude"].as<float>());
			}
		}
	}

	auto channels = node["channels"];
	if(!channels)
		return;

	for(auto it : channels)
	{
		auto cnode = it.second;
		if(!cnode["index"])
			continue;
		size_t i = cnode["index"].as<size_t>();
		if(i >= GetChannelCount())
			continue;

		if(HasGainControl(i))
		{
			//Mode first, since the gain may only be settable in manual mode
			if(cnode["gainmode"])
				SetGainMode(i, cnode["gainmode"].as<string>());
			if(cnode["gain"])
				SetGain(i, cnode["gain"].as<float>());
		}

		if(HasRSSI(i) && cnode["rssi"])
			SetRSSIEnabled(i, cnode["rssi"].as<bool>());

		if(HasLevelCorrection(i))
		{
			//Oscilloscope has already loaded the range and offset, which go with this external gain, so don't
			//rescale them like SetExternalGain() would
			if(cnode["externalgain"])
			{
				lock_guard<recursive_mutex> lock(m_cacheMutex);
				m_levelCorrection[i].m_externalGain = cnode["externalgain"].as<float>();
			}
			if(cnode["calfile"])
				SetCalibrationFile(i, cnode["calfile"].as<string>());
		}
	}
}

//This is called by Instrument::m_loaders and is not virtual
//cppcheck-suppress duplInheritedMember
void SCPISDR::DoPreLoadConfiguration(
	int version,
	const YAML::Node& node,
	IDTable& idmap,
	ConfigWarningList& list)
{
}

