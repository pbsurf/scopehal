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

/**
	@file
	@author ngscopeclient contributors
	@brief Implementation of IIOMockContext
	@ingroup sdrdrivers
 */

#ifdef HAS_IIO

#include "scopehal.h"
#include "IIOMockContext.h"

using namespace std;

static const char* g_phy = "ad9361-phy";
static const char* g_rxData = "cf-ad9361-lpc";
static const char* g_txData = "cf-ad9361-dds-core-lpc";

//Sample rate limits without FIR decimation
static const int64_t g_minSampleRateHz = 2083334;
static const int64_t g_maxSampleRateHz = 61440000;
static const int64_t g_minBandwidthHz = 200000;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

unique_ptr<IIOContext> IIOMockContext::Create(const string& uri)
{
	string variant = uri.substr(strlen("mock:"));
	if(variant.empty())
		variant = "ad9363";

	if(variant == "ad9363")
		return unique_ptr<IIOContext>(new IIOMockContext(uri, { variant, "Analog Devices PlutoSDR Rev.B (Z7010-AD9363A)",
			1, 325000000, 3800000000, 20000000, -1, 73 }));
	else if(variant == "ad9361")
		return unique_ptr<IIOContext>(new IIOMockContext(uri, { variant, "Analog Devices Mock AD9361 2R2T",
			2, 70000000, 6000000000, 56000000, -3, 71 }));

	LogError("Unknown mock IIO device \"%s\" (supported: ad9363, ad9361)\n", variant.c_str());
	return nullptr;
}

IIOMockContext::IIOMockContext(const string& uri, const Variant& variant)
	: m_uri(uri)
	, m_variant(variant)
	, m_sampleIndex(0)
	, m_rng(0)
{
	m_ctxAttrs["hw_model"] = variant.hwModel;
	m_ctxAttrs["hw_model_variant"] = "1";
	m_ctxAttrs["hw_serial"] = "MOCK-" + variant.name + "-0001";
	m_ctxAttrs["fw_version"] = "v0.38";
	m_ctxAttrs["uri"] = uri;

	m_devices = { g_phy, g_rxData, g_txData };

	//Control device
	for(size_t i=0; i<variant.numChannels; i++)
	{
		string id = string("voltage") + to_string(i);
		for(bool output : { false, true })
		{
			AddChannel(g_phy, output, id);
			AddAttr(g_phy, id, output, "rf_bandwidth", "2000000");
			AddAttr(g_phy, id, output, "sampling_frequency", "2500000");
			AddAttr(g_phy, id, output, "rf_port_select", output ? "A" : "A_BALANCED");

			if(output)
			{
				AddAttr(g_phy, id, output, "hardwaregain", "-10.000000 dB");
				AddAttr(g_phy, id, output, "hardwaregain_available", "[-89.75 0.25 0]", false);
			}
			else
			{
				AddAttr(g_phy, id, output, "hardwaregain", "71.000000 dB");
				AddAttr(g_phy, id, output, "gain_control_mode", "slow_attack");
				AddAttr(g_phy, id, output, "rssi", "70.00 dB", false);

				//Supported ranges are in IIO "[min step max]" format
				AddAttr(g_phy, id, output, "gain_control_mode_available", "manual fast_attack slow_attack hybrid", false);
				AddAttr(g_phy, id, output, "hardwaregain_available",
					"[" + to_string(static_cast<int>(variant.minGainDb)) + " 1 " +
					to_string(static_cast<int>(variant.maxGainDb)) + "]", false);
				AddAttr(g_phy, id, output, "rf_bandwidth_available",
					"[" + to_string(g_minBandwidthHz) + " 1 " + to_string(variant.maxBandwidthHz) + "]", false);
				AddAttr(g_phy, id, output, "sampling_frequency_available",
					"[" + to_string(g_minSampleRateHz) + " 1 " + to_string(g_maxSampleRateHz) + "]", false);
			}
		}
	}

	//Local oscillators (both are output channels, RX_LO is altvoltage0 and TX_LO is altvoltage1)
	AddChannel(g_phy, true, "altvoltage0", "RX_LO");
	AddAttr(g_phy, "altvoltage0", true, "frequency", "2400000000");
	AddAttr(g_phy, "altvoltage0", true, "frequency_available",
		"[" + to_string(variant.minLoHz) + " 1 " + to_string(variant.maxLoHz) + "]", false);
	AddChannel(g_phy, true, "altvoltage1", "TX_LO");
	AddAttr(g_phy, "altvoltage1", true, "frequency", "2400000000");
	AddAttr(g_phy, "altvoltage1", true, "frequency_available",
		"[" + to_string(variant.minLoHz) + " 1 " + to_string(variant.maxLoHz) + "]", false);

	AddChannel(g_phy, false, "temp0");
	AddAttr(g_phy, "temp0", false, "input", "45000", false);

	//DDS core. Every transmit path has two tones, each of which is a pair of DDSs (one for I and one for Q)
	for(size_t n=0; n<variant.numChannels; n++)
	{
		size_t k = n * 4;
		for(char iq : { 'I', 'Q' })
		{
			for(size_t f=1; f<=2; f++)
			{
				string id = string("altvoltage") + to_string(k ++);
				string name = string("TX") + to_string(n+1) + "_" + iq + "_F" + to_string(f);
				AddChannel(g_txData, true, id, name);

				//F1 is running by default like it is in the stock firmware, F2 is off
				AddAttr(g_txData, id, true, "frequency", "1000000");
				AddAttr(g_txData, id, true, "scale", f == 1 ? "0.250000" : "0.000000");
				AddAttr(g_txData, id, true, "phase", iq == 'I' ? "90000" : "0");
				AddAttr(g_txData, id, true, "raw", f == 1 ? "1" : "0");
			}
		}
	}

	//TODO: streaming channels and buffers on the data devices (cf-ad9361-lpc / cf-ad9361-dds-core-lpc)
}

IIOMockContext::~IIOMockContext()
{
}

void IIOMockContext::AddChannel(const string& dev, bool output, const string& id, const string& name)
{
	m_channels[dev + "|" + (output ? "out" : "in") + "|" + id] = id;
	if(!name.empty())
		m_channels[dev + "|" + (output ? "out" : "in") + "|" + name] = id;
}

void IIOMockContext::AddAttr(
	const string& dev, const string& chan, bool output, const string& attr, const string& value, bool writable)
{
	m_attrs[MakeKey(dev, chan, output, attr)] = { value, writable };
}

string IIOMockContext::MakeKey(const string& dev, const string& chan, bool output, const string& attr)
{
	return dev + "|" + chan + "|" + (output ? "out" : "in") + "|" + attr;
}

/**
	@brief Looks up a channel by ID or name (libiio accepts either)

	@return Canonical channel ID, or an empty string if not found
 */
string IIOMockContext::ResolveChannel(const string& dev, const string& chan, bool output)
{
	auto it = m_channels.find(dev + "|" + (output ? "out" : "in") + "|" + chan);
	if(it == m_channels.end())
		return "";
	return it->second;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Context info

string IIOMockContext::GetUri()
{
	return m_uri;
}

string IIOMockContext::GetDescription()
{
	return "Mock IIO context (" + m_variant.name + ")";
}

map<string, string> IIOMockContext::GetAttributes()
{
	return m_ctxAttrs;
}

vector<string> IIOMockContext::GetDeviceNames()
{
	return m_devices;
}

bool IIOMockContext::HasDevice(const string& dev)
{
	return find(m_devices.begin(), m_devices.end(), dev) != m_devices.end();
}

bool IIOMockContext::HasChannel(const string& dev, const string& chan, bool output)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	return !ResolveChannel(dev, chan, output).empty();
}

bool IIOMockContext::HasChannelAttr(const string& dev, const string& chan, bool output, const string& attr)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto id = ResolveChannel(dev, chan, output);
	return !id.empty() && (m_attrs.find(MakeKey(dev, id, output, attr)) != m_attrs.end());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Attribute access

bool IIOMockContext::ReadAttr(
	const string& dev, const string& chan, bool output, const string& attr, string& value)
{
	auto it = m_attrs.find(MakeKey(dev, chan, output, attr));
	if(it == m_attrs.end())
	{
		LogError("Failed to read IIO attribute %s/%s/%s: %s\n", dev.c_str(), chan.c_str(), attr.c_str(),
			strerror(ENOENT));
		return false;
	}

	value = it->second.value;
	return true;
}

/**
	@brief Writes an attribute, applying (approximate) AD936x range checks
 */
bool IIOMockContext::WriteAttr(
	const string& dev, const string& chan, bool output, const string& attr, const string& value)
{
	string key = MakeKey(dev, chan, output, attr);
	string what = dev + "/" + chan + "/" + attr;

	auto it = m_attrs.find(key);
	if(it == m_attrs.end())
	{
		LogError("Failed to write IIO attribute %s: %s\n", what.c_str(), strerror(ENOENT));
		return false;
	}
	if(!it->second.writable)
	{
		LogError("Failed to write IIO attribute %s: %s\n", what.c_str(), strerror(EACCES));
		return false;
	}

	//DDS core: frequency is limited to half of the transmit sample rate, scale is a fraction of full scale
	if(dev == g_txData)
	{
		char* end = nullptr;
		double v = strtod(value.c_str(), &end);
		double hi;
		if(attr == "frequency")
		{
			string rate;
			hi = ReadAttr(g_phy, "voltage0", true, "sampling_frequency", rate) ? strtod(rate.c_str(), nullptr) / 2 : 0;
		}
		else if(attr == "scale")
			hi = 1;
		else if(attr == "phase")
			hi = 360000;
		else if(attr == "raw")
			hi = 1;
		else
			hi = -1;

		if( (hi < 0) || (end == value.c_str()) || (v < 0) || (v > hi) )
		{
			LogError("Failed to write IIO attribute %s = \"%s\": %s\n", what.c_str(), value.c_str(), strerror(EINVAL));
			return false;
		}

		char tmp[64];
		if(attr == "scale")
			snprintf(tmp, sizeof(tmp), "%.6f", v);
		else
			snprintf(tmp, sizeof(tmp), "%" PRId64, static_cast<int64_t>(v));
		it->second.value = tmp;
		return true;
	}

	//Numeric attributes: parse and apply range rules
	if( (attr == "frequency") || (attr == "sampling_frequency") || (attr == "rf_bandwidth") )
	{
		char* end = nullptr;
		errno = 0;
		int64_t v = strtoll(value.c_str(), &end, 10);
		if( (end == value.c_str()) || (errno != 0) )
		{
			LogError("Failed to write IIO attribute %s = \"%s\": %s\n", what.c_str(), value.c_str(), strerror(EINVAL));
			return false;
		}

		if(attr == "frequency")
		{
			if( (v < m_variant.minLoHz) || (v > m_variant.maxLoHz) )
			{
				LogError("Failed to write IIO attribute %s = %" PRId64 ": %s\n", what.c_str(), v, strerror(EINVAL));
				return false;
			}
		}

		//Sample rate is shared by RX and TX and out of range values are rejected
		else if(attr == "sampling_frequency")
		{
			if( (v < g_minSampleRateHz) || (v > g_maxSampleRateHz) )
			{
				LogError("Failed to write IIO attribute %s = %" PRId64 ": %s\n", what.c_str(), v, strerror(EINVAL));
				return false;
			}

			for(auto& it2 : m_attrs)
			{
				if( (it2.first.find(string(g_phy) + "|voltage") == 0) &&
					(it2.first.size() >= attr.size()) &&
					(it2.first.compare(it2.first.size() - attr.size(), attr.size(), attr) == 0) )
				{
					it2.second.value = to_string(v);
				}
			}
			return true;
		}

		//Analog bandwidth is clamped to the supported range
		else
			v = min(max(v, g_minBandwidthHz), m_variant.maxBandwidthHz);

		it->second.value = to_string(v);
		return true;
	}

	if(attr == "hardwaregain")
	{
		//RX gain is only settable in manual mode
		if(!output)
		{
			string mode;
			if(ReadAttr(dev, chan, output, "gain_control_mode", mode) && (mode != "manual"))
			{
				LogError("Failed to write IIO attribute %s = \"%s\": %s (gain_control_mode is %s)\n",
					what.c_str(), value.c_str(), strerror(EPERM), mode.c_str());
				return false;
			}
		}

		char* end = nullptr;
		double v = strtod(value.c_str(), &end);
		double lo = output ? -89.75 : m_variant.minGainDb;
		double hi = output ? 0 : m_variant.maxGainDb;
		if( (end == value.c_str()) || (v < lo) || (v > hi) )
		{
			LogError("Failed to write IIO attribute %s = \"%s\": %s\n", what.c_str(), value.c_str(), strerror(EINVAL));
			return false;
		}

		//The transmit attenuation is in steps of 0.25 dB
		if(output)
			v = round(v * 4) / 4;

		char tmp[64];
		snprintf(tmp, sizeof(tmp), "%.6f dB", v);
		it->second.value = tmp;
		return true;
	}

	if(attr == "gain_control_mode")
	{
		if( (value != "manual") && (value != "slow_attack") && (value != "fast_attack") && (value != "hybrid") )
		{
			LogError("Failed to write IIO attribute %s = \"%s\": %s\n", what.c_str(), value.c_str(), strerror(EINVAL));
			return false;
		}
	}

	it->second.value = value;
	return true;
}

bool IIOMockContext::ReadDeviceAttr(const string& dev, const string& attr, string& value)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	return ReadAttr(dev, "", false, attr, value);
}

bool IIOMockContext::WriteDeviceAttr(const string& dev, const string& attr, const string& value)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	return WriteAttr(dev, "", false, attr, value);
}

bool IIOMockContext::ReadChannelAttr(
	const string& dev, const string& chan, bool output, const string& attr, string& value)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto id = ResolveChannel(dev, chan, output);
	if(id.empty())
	{
		LogError("IIO channel \"%s/%s\" (%s) not found\n", dev.c_str(), chan.c_str(), output ? "out" : "in");
		return false;
	}
	return ReadAttr(dev, id, output, attr, value);
}

bool IIOMockContext::WriteChannelAttr(
	const string& dev, const string& chan, bool output, const string& attr, const string& value)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto id = ResolveChannel(dev, chan, output);
	if(id.empty())
	{
		LogError("IIO channel \"%s/%s\" (%s) not found\n", dev.c_str(), chan.c_str(), output ? "out" : "in");
		return false;
	}
	return WriteAttr(dev, id, output, attr, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Streaming

/**
	@brief Simulated RF environment: frequency in Hz and amplitude as a fraction of ADC full scale
 */
struct MockTone
{
	double freq;
	double amplitude;
};

static const MockTone g_mockTones[] =
{
	{ 433920000, 0.30 },
	{ 915000000, 0.30 },
	{ 2400500000, 0.50 },
	{ 2412000000, 0.40 },
	{ 2437000000, 0.30 }
};

/**
	@brief Synthesizes a block of samples

	There's no buffer, samples are generated on demand, so they're never out of date and there's nothing to discard.
 */
bool IIOMockContext::CaptureBlock(
	const string& dev,
	const vector<string>& channels,
	size_t depth,
	size_t /*kernelBuffers*/,
	size_t /*discard*/,
	vector<vector<int16_t> >& data)
{
	if(dev != g_rxData)
	{
		LogError("Mock IIO device \"%s\" is not capable of streaming\n", dev.c_str());
		return false;
	}

	//Sanity check the request and figure out which RX path (0 = RX1) each channel belongs to
	//Channels come in I/Q pairs: voltage0 = RX1 I, voltage1 = RX1 Q, voltage2 = RX2 I, ...
	const size_t maxDepth = 16 * 1024 * 1024;
	if( (depth == 0) || (depth > maxDepth) )
	{
		LogError("Invalid mock IIO buffer size %zu\n", depth);
		return false;
	}
	vector<size_t> path;
	vector<bool> isQ;
	for(auto& name : channels)
	{
		unsigned int index;
		char extra;
		if( (1 != sscanf(name.c_str(), "voltage%u%c", &index, &extra)) || (index >= 2*m_variant.numChannels) )
		{
			LogError("Mock IIO device \"%s\" has no input scan element \"%s\"\n", dev.c_str(), name.c_str());
			return false;
		}
		path.push_back(index / 2);
		isQ.push_back(index & 1);
	}

	int64_t lo;
	int64_t rate;
	int64_t bw;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		if( !ReadChannelAttrInt(g_phy, "altvoltage0", true, "frequency", lo) ||
			!ReadChannelAttrInt(g_phy, "voltage0", false, "sampling_frequency", rate) ||
			!ReadChannelAttrInt(g_phy, "voltage0", false, "rf_bandwidth", bw) )
		{
			return false;
		}
	}

	//Take as long as a real capture would, but don't hang the caller for ages on huge captures
	double duration = static_cast<double>(depth) / rate;
	this_thread::sleep_for(chrono::microseconds(static_cast<int64_t>(min(duration, 2.0) * 1e6)));

	uint64_t start;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		start = m_sampleIndex;
		m_sampleIndex += depth;
	}

	//Signal level of each RX path. AGC holds it constant, manual gain scales it.
	vector<double> pathGain;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		for(size_t i=0; i<m_variant.numChannels; i++)
		{
			string id = "voltage" + to_string(i);
			string mode;
			double gainDb = 20;
			ReadAttr(g_phy, id, false, "gain_control_mode", mode);
			string gain;
			if(ReadAttr(g_phy, id, false, "hardwaregain", gain))
				gainDb = strtod(gain.c_str(), nullptr);
			pathGain.push_back( (mode == "manual") ? pow(10, (gainDb - 20) / 20) : 1.0);
		}
	}

	//Signals outside the analog filter or the Nyquist bandwidth are gone
	double halfBand = min(bw, rate) / 2.0;

	//Noise floor of a few counts
	normal_distribution<double> noise(0, 6);

	data.clear();
	data.resize(channels.size());
	const double twoPi = 2 * M_PI;
	for(size_t i=0; i<channels.size(); i++)
	{
		//The second RX path sees a slightly weaker and phase shifted version of the same signals
		double gain = ( (path[i] == 0) ? 1.0 : 0.6 ) * pathGain[path[i]];
		double phaseOffset = path[i] * M_PI / 3;

		vector<double> acc(depth, 0.0);
		for(auto& tone : g_mockTones)
		{
			double fbb = tone.freq - lo;
			if(fabs(fbb) > halfBand)
				continue;

			double amp = tone.amplitude * gain * 2047;
			for(size_t j=0; j<depth; j++)
			{
				double cycles = fmod(fbb * static_cast<double>(start + j) / rate, 1.0);
				double phase = twoPi * cycles + phaseOffset;
				acc[j] += amp * (isQ[i] ? sin(phase) : cos(phase));
			}
		}

		auto& out = data[i];
		out.resize(depth);
		lock_guard<recursive_mutex> lock(m_mutex);
		for(size_t j=0; j<depth; j++)
		{
			double v = acc[j] + noise(m_rng);
			out[j] = static_cast<int16_t>(min(2047.0, max(-2048.0, round(v))));
		}
	}

	return true;
}

void IIOMockContext::StopCapture()
{
	//Nothing to do, we don't keep a buffer open
}

#endif
