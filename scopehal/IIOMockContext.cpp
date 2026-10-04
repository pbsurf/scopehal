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
#include <complex>

using namespace std;

static const char* g_phy = "ad9361-phy";
static const char* g_rxData = "cf-ad9361-lpc";
static const char* g_txData = "cf-ad9361-dds-core-lpc";

//Receive gain the mock's AGC settles at
static const char* g_mockAgcGain = "20.000000 dB";

//Sample rate limits without FIR decimation
static const int64_t g_minSampleRateHz = 2083334;
static const int64_t g_maxSampleRateHz = 61440000;
static const int64_t g_minBandwidthHz = 200000;

/**
	@brief Gets the sample rate the AD936x actually runs at when asked for a given rate

	The sample clock is divided down from a fractional-N baseband PLL (40 MHz reference, fixed modulus, 715 to 1430 MHz)
	and the driver truncates at each step, so some rates can't be hit exactly. 30.72 MS/s reads back as 30719999 Hz,
	for example. This models the PLL with a power of two divider, which is enough to reproduce that.
 */
static int64_t ActualSampleRate(int64_t rate)
{
	const int64_t ref = 40000000;
	const int64_t modulus = 2088960;

	int64_t div = 2;
	while(rate * div < 715000000)
		div *= 2;

	int64_t target = rate * div;
	int64_t n = target / ref;
	int64_t frac = (target - n*ref) * modulus / ref;
	return (n*ref + frac*ref/modulus) / div;
}

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
				//Running AGC, which settles at 20 dB
				AddAttr(g_phy, id, output, "hardwaregain", g_mockAgcGain);
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
					it2.second.value = to_string(ActualSampleRate(v));
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

		//AGC takes over the gain
		if(value != "manual")
		{
			auto gain = m_attrs.find(MakeKey(dev, chan, output, "hardwaregain"));
			if(gain != m_attrs.end())
				gain->second.value = g_mockAgcGain;
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
	@brief Simulated RF environment: frequency in Hz and power at the RX input in dBm
 */
struct MockTone
{
	double freq;
	double dbm;
};

static const MockTone g_mockTones[] =
{
	{ 433920000, -55 },
	{ 915000000, -60 },
	{ 2400500000, -40 },
	{ 2412000000, -50 },
	{ 2437000000, -60 }
};

//Each TX path is looped back to the RX path with the same number, as if through a cable and an attenuator
static const double g_loopbackLossDb = 30;

//Output power of a full scale tone (DDS scale 1) at 0 dB transmit attenuation
static const double g_txFullScaleDbm = 7;

//Receiver noise: thermal noise at the input plus the noise figure, and the noise of the ADC itself (in counts)
static const double g_thermalNoiseDbmPerHz = -174;
static const double g_noiseFigureDb = 3;
static const double g_adcNoiseCounts = 0.3;

//ADC counts for a signal at full scale
static const double g_adcFullScale = 2048;

/**
	@brief Gets the amplitude of I and Q, at the RX input, of a tone with the given power

	The units are the ones the driver refers the samples to (full scale at 0 dB gain is 1), and power is (2A)^2 / 50
	ohms like the Complex FFT, so what it displays is the power that went in.
 */
static double InputAmplitude(double dbm)
{
	return sqrt(50 * pow(10, (dbm - 30) / 10)) / 2;
}

/**
	@brief A complex exponential in the baseband of an RX path

	The I samples are the real part of amplitude * exp(j * 2pi * freq * t), the Q samples are the imaginary part.
 */
struct MockExponential
{
	double freq;
	complex<double> amplitude;
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

	//Gain of each RX path: AGC always settles at 20 dB
	vector<double> gainDb;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		for(size_t i=0; i<m_variant.numChannels; i++)
		{
			string id = "voltage" + to_string(i);
			string mode;
			string gain;
			double g = 20;
			if(ReadAttr(g_phy, id, false, "gain_control_mode", mode) && (mode == "manual") &&
				ReadAttr(g_phy, id, false, "hardwaregain", gain))
			{
				g = strtod(gain.c_str(), nullptr);
			}
			gainDb.push_back(g);
		}
	}

	//Signals outside the analog filter or the Nyquist bandwidth are gone
	double halfBand = min(bw, rate) / 2.0;

	//Everything each RX path receives, in ADC counts
	vector<vector<MockExponential> > signals(m_variant.numChannels);
	vector<double> noiseCounts;
	for(size_t p=0; p<m_variant.numChannels; p++)
	{
		auto& sig = signals[p];
		double gain = pow(10, gainDb[p] / 20);

		//The environment. The second RX path sees a slightly weaker and phase shifted version of the same signals.
		double envGain = ( (p == 0) ? 1.0 : 0.6 ) * gain * g_adcFullScale;
		for(auto& tone : g_mockTones)
		{
			double fbb = tone.freq - lo;
			if(fabs(fbb) <= halfBand)
				sig.push_back({ fbb, polar(InputAmplitude(tone.dbm) * envGain, p * M_PI / 3) });
		}

		//The TX path looped back to it. Each DDS is scale * sin(2pi f t + phase), with I and Q as the real and
		//imaginary parts of the transmitted signal, so a DDS is a pair of exponentials at +/- its frequency.
		{
			lock_guard<recursive_mutex> lock(m_mutex);
			string txId = "voltage" + to_string(p);
			int64_t txLo;
			double txGainDb;
			if(ReadChannelAttrInt(g_phy, "altvoltage1", true, "frequency", txLo) &&
				ReadChannelAttrDouble(g_phy, txId, true, "hardwaregain", txGainDb))
			{
				double link = InputAmplitude(g_txFullScaleDbm + txGainDb - g_loopbackLossDb) * gain * g_adcFullScale;
				double offset = static_cast<double>(txLo - lo);
				for(size_t k=0; k<4; k++)
				{
					string id = "altvoltage" + to_string(p*4 + k);
					bool q = (k >= 2);
					string raw;
					double freq;
					double scale;
					double phase;
					if( !ReadAttr(g_txData, id, true, "raw", raw) || (raw != "1") ||
						!ReadChannelAttrDouble(g_txData, id, true, "frequency", freq) ||
						!ReadChannelAttrDouble(g_txData, id, true, "scale", scale) ||
						!ReadChannelAttrDouble(g_txData, id, true, "phase", phase) )
					{
						continue;
					}

					//sin(x) = (exp(jx) - exp(-jx)) / 2j, and Q is the imaginary part
					complex<double> rail = q ? complex<double>(0, 1) : complex<double>(1, 0);
					complex<double> half = rail * link * scale / complex<double>(0, 2);
					double rad = phase / 1000 * M_PI / 180;
					for(int sign : { 1, -1 })
					{
						double fbb = offset + sign*freq;
						if(fabs(fbb) <= halfBand)
							sig.push_back({ fbb, static_cast<double>(sign) * half * polar(1.0, sign * rad) });
					}
				}
			}
		}

		//Thermal noise over the sample rate, split between I and Q, and the ADC's own noise on top
		double thermal = sqrt(50 * pow(10, (g_thermalNoiseDbmPerHz + g_noiseFigureDb - 30) / 10) * rate / 8);
		thermal *= gain * g_adcFullScale;
		noiseCounts.push_back(sqrt(thermal*thermal + g_adcNoiseCounts*g_adcNoiseCounts));
	}

	data.clear();
	data.resize(channels.size());
	const double twoPi = 2 * M_PI;
	for(size_t i=0; i<channels.size(); i++)
	{
		vector<double> acc(depth, 0.0);
		for(auto& e : signals[path[i]])
		{
			for(size_t j=0; j<depth; j++)
			{
				double cycles = fmod(e.freq * static_cast<double>(start + j) / rate, 1.0);
				auto v = e.amplitude * polar(1.0, twoPi * cycles);
				acc[j] += isQ[i] ? v.imag() : v.real();
			}
		}

		//Clip at full scale like the ADC does
		normal_distribution<double> noise(0, noiseCounts[path[i]]);
		auto& out = data[i];
		out.resize(depth);
		lock_guard<recursive_mutex> lock(m_mutex);
		for(size_t j=0; j<depth; j++)
		{
			double v = acc[j] + noise(m_rng);
			out[j] = static_cast<int16_t>(min(g_adcFullScale - 1, max(-g_adcFullScale, round(v))));
		}
	}

	return true;
}

void IIOMockContext::StopCapture()
{
	//Nothing to do, we don't keep a buffer open
}

#endif
