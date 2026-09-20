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
			1, 325000000, 3800000000, 20000000 }));
	else if(variant == "ad9361")
		return unique_ptr<IIOContext>(new IIOMockContext(uri, { variant, "Analog Devices Mock AD9361 2R2T",
			2, 70000000, 6000000000, 56000000 }));

	LogError("Unknown mock IIO device \"%s\" (supported: ad9363, ad9361)\n", variant.c_str());
	return nullptr;
}

IIOMockContext::IIOMockContext(const string& uri, const Variant& variant)
	: m_uri(uri)
	, m_variant(variant)
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
				AddAttr(g_phy, id, output, "hardwaregain", "-10.000000 dB");
			else
			{
				AddAttr(g_phy, id, output, "hardwaregain", "71.000000 dB");
				AddAttr(g_phy, id, output, "gain_control_mode", "slow_attack");
				AddAttr(g_phy, id, output, "rssi", "70.00 dB", false);
			}
		}
	}

	//Local oscillators (both are output channels, RX_LO is altvoltage0 and TX_LO is altvoltage1)
	AddChannel(g_phy, true, "altvoltage0", "RX_LO");
	AddAttr(g_phy, "altvoltage0", true, "frequency", "2400000000");
	AddChannel(g_phy, true, "altvoltage1", "TX_LO");
	AddAttr(g_phy, "altvoltage1", true, "frequency", "2400000000");

	AddChannel(g_phy, false, "temp0");
	AddAttr(g_phy, "temp0", false, "input", "45000", false);

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
		double lo = output ? -89.75 : -3;
		double hi = output ? 0 : 71;
		if( (end == value.c_str()) || (v < lo) || (v > hi) )
		{
			LogError("Failed to write IIO attribute %s = \"%s\": %s\n", what.c_str(), value.c_str(), strerror(EINVAL));
			return false;
		}

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

#endif
