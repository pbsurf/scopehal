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

bool SCPISDR::IsTxToneEnabled(size_t /*tx*/, size_t /*tone*/)
{
	return false;
}

void SCPISDR::SetTxToneEnabled(size_t /*tx*/, size_t /*tone*/, bool /*enabled*/)
{
	//no-op
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
				tone["enabled"] = IsTxToneEnabled(i, j);
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
		if(!HasGainControl(i))
			continue;

		YAML::Node channelNode = channels["ch" + to_string(i)];
		channelNode["index"] = i;
		auto mode = GetGainMode(i);
		if(!mode.empty())
			channelNode["gainmode"] = mode;
		channelNode["gain"] = GetGain(i);
	}
}

//This is called by Instrument::m_preloaders and is not virtual
//cppcheck-suppress duplInheritedMember
void SCPISDR::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& /*idmap*/)
{
	//Oscilloscope saves the span, but doesn't restore it
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
				if(tone["enabled"])
					SetTxToneEnabled(i, j, tone["enabled"].as<bool>());
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
		if(i >= GetChannelCount() || !HasGainControl(i))
			continue;

		//Mode first, since the gain may only be settable in manual mode
		if(cnode["gainmode"])
			SetGainMode(i, cnode["gainmode"].as<string>());
		if(cnode["gain"])
			SetGain(i, cnode["gain"].as<float>());
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

