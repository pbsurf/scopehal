/***********************************************************************************************************************
*                                                                                                                      *
* ngscopeclient                                                                                                        *
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
	@author Andrew D. Zonenberg
	@brief Implementation of DemoOscilloscope

	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"
#include "DemoOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPINullTransport since this driver does not connect to physical hardware
 */
DemoOscilloscope::DemoOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport, false)
	, SCPIInstrument(transport, false)
	, m_extTrigger(nullptr)
	, m_triggerOffset(0)
	, m_triggerForced(false)
	, m_lastTriggerEdgeRising(false)
	, m_triggerChannel(-1)
	, m_triggerLevel(0)
	, m_triggerEdgeType(EdgeTrigger::EDGE_RISING)
{
	for(int i=0; i<4; i++)
	{
		m_rng[i] = new minstd_rand(m_rd());
		m_source[i] = new TestWaveformSource(*m_rng[i]);
	}

	m_digitalSource = new TestDigitalWaveformSource();

	m_model = "Oscilloscope Simulator";
	m_vendor = "Antikernel Labs";
	m_serial = "12345";

	//Create a bunch of channels
	static const char* colors[8] =
	{ "#ffff00", "#ff6abc", "#00ffff", "#00c100", "#d7ffd7", "#8482ff", "#ff0000", "#ff8000" };

	for(size_t i=0; i<4; i++)
	{
		m_channels.push_back(
			new OscilloscopeChannel(
				this,
				string("CH") + to_string(i+1),
				colors[i],
				Unit(Unit::UNIT_FS),
				Unit(Unit::UNIT_VOLTS),
				Stream::STREAM_TYPE_ANALOG,
				i));

		//initial configuration is 1V p-p for each
		m_channelsEnabled[i] = true;
		m_channelCoupling[i] = OscilloscopeChannel::COUPLE_DC_50;
		m_channelAttenuation[i] = 10;
		m_channelBandwidth[i] = 0;
		m_channelVoltageRange[i] = 1;
		m_channelOffset[i] = 0;

		m_channelModes[i] = CHANNEL_MODE_NOISE_LPF;
	}

	char chn[32];
	for(size_t i=0; i<16; i++)
	{
		snprintf(chn, sizeof(chn), "D%zu", i);
		auto chan = new OscilloscopeChannel(
			this,
			chn,
			GetDefaultChannelColor(m_channels.size()),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_COUNTS),
			Stream::STREAM_TYPE_DIGITAL,
			m_channels.size());
		m_channels.push_back(chan);
		m_digitalChannels.push_back(chan);
	}

	m_sweepFreq = 1e9;

	//Default sampling configuration
	m_depth = 1e6;
	m_rate = 50e9;

	m_channels[0]->SetDisplayName("Tone");
	m_channels[1]->SetDisplayName("Ramp");
	m_channels[2]->SetDisplayName("PRBS31");
	m_channels[3]->SetDisplayName("8B10B");

	m_channels[4]->SetDisplayName("SPI-CS");
	m_channels[5]->SetDisplayName("SPI-SCLK");
	m_channels[6]->SetDisplayName("SPI-MOSI");
	m_channels[7]->SetDisplayName("UART-0");
	m_channels[8]->SetDisplayName("UART-0-Clk");
	m_channels[9]->SetDisplayName("UART-1");
	m_channels[10]->SetDisplayName("UART-1-Clk");
	m_channels[11]->SetDisplayName("Parallel-Clk");

	m_channels[12]->SetDisplayName("Parallel-0");
	m_channels[13]->SetDisplayName("Parallel-1");
	m_channels[14]->SetDisplayName("Parallel-2");
	m_channels[15]->SetDisplayName("Parallel-3");
	m_channels[16]->SetDisplayName("Parallel-4");
	m_channels[17]->SetDisplayName("Parallel-5");
	m_channels[18]->SetDisplayName("Parallel-6");
	m_channels[19]->SetDisplayName("Parallel-7");

	//Create Vulkan objects for the waveform conversion
	InitVulkanQueue("DemoOscilloscope");
}

vector<Oscilloscope::DigitalBank> DemoOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;

	for(size_t n = 0; n < 2; n++)
	{
		DigitalBank bank;

		for(size_t i = 0; i < 8; i++)
			bank.push_back(m_digitalChannels[i + n * 8]);

		banks.push_back(bank);
	}

	return banks;
}


DemoOscilloscope::~DemoOscilloscope()
{
	LogTrace("Shutting down demo scope\n");

	for(int i=0; i<4; i++)
	{
		delete m_source[i];
		delete m_rng[i];
	}
	delete m_digitalSource;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

string DemoOscilloscope::IDPing()
{
	return "";
}

string DemoOscilloscope::GetTransportName()
{
	return "null";
}

string DemoOscilloscope::GetTransportConnectionString()
{
	return "";
}

///@brief Return the constant driver name "demopsu"
string DemoOscilloscope::GetDriverNameInternal()
{
	return "demo";
}

unsigned int DemoOscilloscope::GetInstrumentTypes() const
{
	return INST_OSCILLOSCOPE;
}

uint32_t DemoOscilloscope::GetInstrumentTypesForChannel([[maybe_unused]] size_t i) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

Oscilloscope::TriggerMode DemoOscilloscope::PollTrigger()
{
	if(m_triggerArmed)
		return TRIGGER_MODE_TRIGGERED;
	else
		return TRIGGER_MODE_STOP;
}

void DemoOscilloscope::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void DemoOscilloscope::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void DemoOscilloscope::Stop()
{
	m_triggerArmed = false;
	m_triggerOneShot = false;
	m_triggerForced = false;
}

void DemoOscilloscope::ForceTrigger()
{
	m_triggerForced = true;
	StartSingleTrigger();
}

bool DemoOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void DemoOscilloscope::LoadConfiguration(int version, const YAML::Node& node, IDTable& table)
{
	//Load the channels
	auto& chans = node["channels"];
	for(auto it : chans)
	{
		auto& cnode = it.second;

		//Allocate channel space if we didn't have it yet
		size_t index = cnode["index"].as<int>();
		if(m_channels.size() < (index+1))
			m_channels.resize(index+1);

		//Configure the channel
		Stream::StreamType type = Stream::STREAM_TYPE_PROTOCOL;
		string stype = cnode["type"].as<string>();
		if(stype == "analog")
			type = Stream::STREAM_TYPE_ANALOG;
		else if(stype == "digital")
			type = Stream::STREAM_TYPE_DIGITAL;
		else if(stype == "trigger")
			type = Stream::STREAM_TYPE_TRIGGER;
		auto chan = new OscilloscopeChannel(
			this,
			cnode["name"].as<string>(),
			cnode["color"].as<string>(),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			type,
			index);
		m_channels[index] = chan;

		//Create the channel ID
		table.emplace(cnode["id"].as<int>(), chan);
	}

	//Call the base class to configure everything
	Oscilloscope::LoadConfiguration(version, node, table);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration. Mostly trivial stubs.

bool DemoOscilloscope::IsChannelEnabled(size_t i)
{
	return m_channelsEnabled[i];
}

void DemoOscilloscope::EnableChannel(size_t i)
{
	m_channelsEnabled[i] = true;
}

void DemoOscilloscope::DisableChannel(size_t i)
{
	m_channelsEnabled[i] = false;
}

OscilloscopeChannel::CouplingType DemoOscilloscope::GetChannelCoupling(size_t i)
{
	return m_channelCoupling[i];
}

vector<OscilloscopeChannel::CouplingType> DemoOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	return ret;
}

void DemoOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	m_channelCoupling[i] = type;
}

double DemoOscilloscope::GetChannelAttenuation(size_t i)
{
	return m_channelAttenuation[i];
}

void DemoOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	m_channelAttenuation[i] = atten;
}

unsigned int DemoOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	return m_channelBandwidth[i];
}

void DemoOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	m_channelBandwidth[i] = limit_mhz;
}

float DemoOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	return m_channelVoltageRange[i];
}

void DemoOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	m_channelVoltageRange[i] = range;
}

bool DemoOscilloscope::IsHighRateOffsetCapable([[maybe_unused]] size_t i)
{
	return true;
}

OscilloscopeChannel* DemoOscilloscope::GetExternalTrigger()
{
	return m_extTrigger;
}

float DemoOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	return m_channelOffset[i];
}

void DemoOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	m_channelOffset[i] = offset;
}

vector<uint64_t> DemoOscilloscope::GetSampleRatesNonInterleaved()
{
	uint64_t k = 1000;
	uint64_t m = k * k;
	uint64_t g = k * m;

	vector<uint64_t> ret;
	ret.push_back(1 * g);
	ret.push_back(5 * g);
	ret.push_back(10 * g);
	ret.push_back(25 * g);
	ret.push_back(50 * g);
	ret.push_back(100 * g);
	ret.push_back(200 * g);
	ret.push_back(500 * g);
	return ret;
}

vector<uint64_t> DemoOscilloscope::GetSampleRatesInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> DemoOscilloscope::GetInterleaveConflicts()
{
	//no-op
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> DemoOscilloscope::GetSampleDepthsNonInterleaved()
{
	uint64_t k = 1000;
	uint64_t m = k * k;

	vector<uint64_t> ret;
	ret.push_back(10 * k);
	ret.push_back(100 * k);
	ret.push_back(1 * m);
	ret.push_back(10 * m);
	ret.push_back(50 * m);
	return ret;
}

vector<uint64_t> DemoOscilloscope::GetSampleDepthsInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

uint64_t DemoOscilloscope::GetSampleRate()
{
	return m_rate;
}

uint64_t DemoOscilloscope::GetSampleDepth()
{
	return m_depth;
}

void DemoOscilloscope::SetSampleDepth(uint64_t depth)
{
	m_depth = depth;
}

void DemoOscilloscope::SetSampleRate(uint64_t rate)
{
	m_rate = rate;
}

void DemoOscilloscope::SetTriggerOffset(int64_t offset)
{
	//Trigger point must be within the capture
	int64_t captureDuration = GetSampleDepth() * (FS_PER_SECOND / GetSampleRate());
	m_triggerOffset = max(min(offset, captureDuration), (int64_t)0);
}

int64_t DemoOscilloscope::GetTriggerOffset()
{
	return m_triggerOffset;
}

bool DemoOscilloscope::CanInterleave()
{
	return false;
}

bool DemoOscilloscope::IsInterleaving()
{
	return false;
}

bool DemoOscilloscope::SetInterleaving([[maybe_unused]] bool combine)
{
	return false;
}

void DemoOscilloscope::PushTrigger()
{
	//Make a copy of the trigger settings, since m_trigger may be replaced while we're acquiring.
	//No edge trigger, or no source, means free run.
	lock_guard<recursive_mutex> lock(m_mutex);
	m_triggerChannel = -1;
	auto trig = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(!trig)
		return;

	auto src = trig->GetInput(0).m_channel;
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(src == GetOscilloscopeChannel(i))
			m_triggerChannel = i;
	}
	m_triggerLevel = trig->GetLevel();
	m_triggerEdgeType = trig->GetType();
}

void DemoOscilloscope::PullTrigger()
{
	//no-op
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Waveform degradation modes

vector<string> DemoOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	vector<string> ret;
	ret.push_back("Ideal");
	ret.push_back("10 mV noise");
	ret.push_back("10 mV noise + 300mm ISI channel");
	return ret;
}

size_t DemoOscilloscope::GetADCMode(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	return m_channelModes[channel];
}

void DemoOscilloscope::SetADCMode(size_t channel, size_t mode)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_channelModes[channel] = mode;
}

bool DemoOscilloscope::IsADCModeConfigurable()
{
	return true;
}

bool DemoOscilloscope::IsADCModePerChannel()
{
	return true;
}

vector<Oscilloscope::AnalogBank> DemoOscilloscope::GetAnalogBanks()
{
	vector<AnalogBank> ret;
	for(size_t i=0; i<GetChannelCount(); i++)
		ret.push_back(GetAnalogBank(i));
	return ret;
}

Oscilloscope::AnalogBank DemoOscilloscope::GetAnalogBank(size_t channel)
{
	AnalogBank bank;
	bank.push_back(GetOscilloscopeChannel(channel));
	return bank;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Waveform synthesis

bool DemoOscilloscope::AcquireData()
{
	if(!m_triggerArmed)
		return false;

	// prepare all channels to be 'about to download'
	ChannelsDownloadStarted();

	//Sweeping frequency
	m_sweepFreq += 1e6;
	if(m_sweepFreq > 1.5e9)
		m_sweepFreq = 1.1e9;
	float sweepPeriod = FS_PER_SECOND / m_sweepFreq;

	//Signal degradations
	float noise[4] =
	{
		0.01, 0.01, 0.01, 0.01
	};
	bool lpf2 = false;
	bool lpf3 = false;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		for(size_t i=0; i<4; i++)
		{
			if(m_channelModes[i] == CHANNEL_MODE_IDEAL)
				noise[i] = 0;
		}
		if(m_channelModes[2] == CHANNEL_MODE_NOISE_LPF)
			lpf2 = true;
		if(m_channelModes[3] == CHANNEL_MODE_NOISE_LPF)
			lpf3 = true;
	}

	//Get the trigger configuration. Forced triggers free run for one acquisition.
	auto depth = GetSampleDepth();
	int64_t sampleperiod = FS_PER_SECOND / m_rate;
	int trigChan;
	float trigLevel;
	EdgeTrigger::EdgeType trigType;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		trigChan = m_triggerForced ? -1 : m_triggerChannel;
		trigLevel = m_triggerLevel;
		trigType = m_triggerEdgeType;
	}

	//If triggering, generate an extra capture's worth of data past the trigger point to search for an edge in.
	//The trigger channel has to be generated even if it's not displayed.
	size_t margin = (trigChan >= 0) ? depth : 0;
	bool needed[4+16];
	for(int i=0; i<(4+16); i++)
		needed[i] = m_channelsEnabled[i] || (i == trigChan);

	//Generate waveforms

	auto gendepth = depth + margin;
	WaveformBase* waveforms[4+16] = {nullptr};
	for(int i=0; i<4; i++)
	{
		if(!needed[i])
			continue;

		// Lambda passed to generate waveform methods to update "download" percentage
		ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, 0.0);
		switch(i)
		{
			case 0:
				{
					auto wfm = AllocateAnalogWaveform("NoisySine");
					waveforms[i] = wfm;
					m_source[i]->GenerateNoisySinewave(
						*m_cmdBuf, m_queue, wfm, 0.9, 0.0, 1e6, sampleperiod, gendepth, noise[0]);
				}
				break;

			case 1:
				{
					auto wfm = AllocateAnalogWaveform("NoisySineSum");
					waveforms[i] = wfm;
					m_source[i]->GenerateNoisySinewaveSum(
						*m_cmdBuf, m_queue, wfm, 0.9, 0.0, M_PI_4, 1e6, sweepPeriod, sampleperiod, gendepth, noise[1]);
				}
				break;

			case 2:
				{
					auto wfm = AllocateAnalogWaveform("PRBS31");
					waveforms[i] = wfm;
					m_source[i]->GeneratePRBS31(
						*m_cmdBuf, m_queue, wfm, 0.9, 96969.6, sampleperiod, gendepth, lpf2, noise[2]);
				}
				break;

			case 3:
				{
					auto wfm = AllocateAnalogWaveform("8B10B");
					waveforms[i] = wfm;
					m_source[i]->Generate8b10b(
						*m_cmdBuf, m_queue, wfm, 0.9, 800e3, sampleperiod, gendepth, lpf3, noise[3]);
				}
				break;

			default:
				break;
		}

		ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
	}

	bool spiEnabled = needed[4]||needed[5]||needed[6];
	bool parallelEnabled = false;
	for(int i=11; i<=19; i++)
	{
		if(needed[i])
		{
			parallelEnabled = true;
			break;
		}
	}


	// Prepare SPI data
	SparseDigitalWaveform* cs = nullptr;
	SparseDigitalWaveform* sclk = nullptr;
	SparseDigitalWaveform* mosi = nullptr;
	if(spiEnabled)
	{
		cs = AllocateDigitalWaveform("CS");
		sclk = AllocateDigitalWaveform("SCLK");
		mosi = AllocateDigitalWaveform("MOSI");
		//SPI timing scales with the capture length, so keep it the same as an untriggered capture and repeat it
		m_digitalSource->GenerateSPI(cs,sclk,mosi,sampleperiod, depth);
		for(auto w : {cs, sclk, mosi})
			RepeatWaveform(w, depth, gendepth);
	}

	std::vector<SparseDigitalWaveform*> parallelWfms;
	if(parallelEnabled)
	{	// Prepare Parallel bus data
		auto wfClk = AllocateDigitalWaveform("Parallel-Clk");
		parallelWfms.push_back(wfClk);
		// Parallel lines waveforms
		for(int i = 0 ; i < 8 ; i++)
		{
			auto wf = AllocateDigitalWaveform("Parallel-"+to_string(i));
			parallelWfms.push_back(wf);
		}
		m_digitalSource->GenerateParallel(parallelWfms,sampleperiod,depth);
		for(auto w : parallelWfms)
			RepeatWaveform(w, depth, gendepth);
	}

	for(int i=4; i<(4+16); i++)
	{
		if(!needed[i])
		{
			switch(i)
			{
				case 4:
					if(spiEnabled) delete cs;
					break;
				case 5:
					if(spiEnabled) delete sclk;
					break;
				case 6:
					if(spiEnabled) delete mosi;
					break;
				case 11:
				case 12:
				case 13:
				case 14:
				case 15:
				case 16:
				case 17:
				case 18:
				case 19:
					// Paralle bus
					if(parallelEnabled) delete parallelWfms[i-11];
					break;
			}

			continue;
		}

		// Lambda passed to generate waveform methods to update "download" percentage
		ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, 0.0);
		switch(i)
		{
			case 4:
				waveforms[i] = cs;
				break;
			case 5:
				waveforms[i] = sclk;
				break;
			case 6:
				waveforms[i] = mosi;
				break;
			case 11:
			case 12:
			case 13:
			case 14:
			case 15:
			case 16:
			case 17:
			case 18:
			case 19:
				// Paralle bus
				waveforms[i] = parallelWfms[i-11];
				break;
			default:
				if(i%2 == 1)
				{
					auto wfm = AllocateDigitalWaveform("UART");
					waveforms[i] = wfm;
					m_digitalSource->GenerateUART(wfm, sampleperiod, gendepth);
				}
				else
				{
					auto wfm = AllocateDigitalWaveform("UART-Clk");
					waveforms[i] = wfm;
					m_digitalSource->GenerateUARTClock(wfm, sampleperiod, gendepth);
				}
				break;
		}

		ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
	}

	//Look for a trigger edge at or after the trigger point, then crop all channels so it lands there
	int64_t triggerPhase = 0;
	if(trigChan >= 0)
	{
		int64_t offset = min(m_triggerOffset, (int64_t)depth * sampleperiod);
		int64_t tcross;
		bool rising;
		if(!waveforms[trigChan] || !FindTriggerEdge(
			waveforms[trigChan], trigLevel, trigType, offset, offset + (int64_t)margin * sampleperiod, tcross, rising))
		{
			//No trigger, discard everything and try again next time
			for(int i=0; i<(4+16); i++)
			{
				if(!waveforms[i])
					continue;
				if(i < 4)
					AddWaveformToAnalogPool(waveforms[i]);
				else
					AddWaveformToDigitalPool(waveforms[i]);
			}
			ChannelsDownloadFinished();
			return false;
		}
		m_lastTriggerEdgeRising = rising;

		//Crop so the crossing is at the trigger offset. Sub-sample remainder goes in the trigger phase.
		int64_t delta = tcross - offset;
		size_t start = delta / sampleperiod;
		triggerPhase = start*sampleperiod - delta;
		for(int i=0; i<(4+16); i++)
		{
			if(waveforms[i])
				CropWaveform(waveforms[i], start, depth);
		}
	}

	//Get rid of anything we only generated to trigger on
	for(int i=0; i<(4+16); i++)
	{
		if(waveforms[i] && !m_channelsEnabled[i])
		{
			if(i < 4)
				AddWaveformToAnalogPool(waveforms[i]);
			else
				AddWaveformToDigitalPool(waveforms[i]);
			waveforms[i] = nullptr;
		}
	}

	SequenceSet s;
	for(int i=0; i<(4+16); i++)
		s[GetOscilloscopeChannel(i)] = waveforms[i];

	//Timestamp the waveform(s)
	double now = GetTime();
	time_t start = now;
	double tfrac = now - start;
	int64_t fs = tfrac * FS_PER_SECOND;
	for(auto it : s)
	{
		auto wfm = it.second;
		if(!wfm)
			continue;

		wfm->m_startTimestamp = start;
		wfm->m_startFemtoseconds = fs;
		wfm->m_triggerPhase = triggerPhase;
	}

	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	if(m_triggerOneShot)
		m_triggerArmed = false;
	m_triggerForced = false;

	// Tell the download monitor that waveform download has finished
	ChannelsDownloadFinished();

	return true;
}


/**
	@brief Finds the first edge matching the trigger condition

	@param wfm		Waveform to search
	@param level	Trigger level (ignored for digital waveforms)
	@param type		Edge type to match
	@param tmin		Earliest allowable crossing time, in fs
	@param tmax		Latest allowable crossing time, in fs
	@param tcross	Crossing time, in fs from the start of the waveform (interpolated for analog waveforms)
	@param rising	True if the matched edge was rising

	@return True if a matching edge was found
 */
bool DemoOscilloscope::FindTriggerEdge(
	WaveformBase* wfm,
	float level,
	EdgeTrigger::EdgeType type,
	int64_t tmin,
	int64_t tmax,
	int64_t& tcross,
	bool& rising)
{
	//Alternating triggers look for the opposite of whatever we found last time
	bool wantRising = (type == EdgeTrigger::EDGE_RISING) || (type == EdgeTrigger::EDGE_ANY);
	bool wantFalling = (type == EdgeTrigger::EDGE_FALLING) || (type == EdgeTrigger::EDGE_ANY);
	if(type == EdgeTrigger::EDGE_ALTERNATING)
	{
		wantRising = !m_lastTriggerEdgeRising;
		wantFalling = m_lastTriggerEdgeRising;
	}

	wfm->PrepareForCpuAccess();

	auto ua = dynamic_cast<UniformAnalogWaveform*>(wfm);
	if(ua)
	{
		int64_t ts = ua->m_timescale;
		size_t len = ua->size();
		size_t istart = max((int64_t)1, tmin / ts);
		for(size_t i=istart; i<len; i++)
		{
			float a = ua->m_samples[i-1];
			float b = ua->m_samples[i];
			bool r = (a < level) && (b >= level);
			bool f = (a > level) && (b <= level);
			if( !( (r && wantRising) || (f && wantFalling) ) )
				continue;

			//Interpolate the crossing
			float frac = (level - a) / (b - a);
			int64_t t = (i-1)*ts + static_cast<int64_t>(frac * ts);
			if(t < tmin)
				continue;
			if(t > tmax)
				return false;

			tcross = t;
			rising = r;
			return true;
		}
		return false;
	}

	auto sd = dynamic_cast<SparseDigitalWaveform*>(wfm);
	if(sd)
	{
		int64_t ts = sd->m_timescale;
		size_t len = sd->size();
		for(size_t i=1; i<len; i++)
		{
			bool a = sd->m_samples[i-1];
			bool b = sd->m_samples[i];
			if( (a == b) || !( (b && wantRising) || (!b && wantFalling) ) )
				continue;

			int64_t t = sd->m_offsets[i]*ts;
			if(t < tmin)
				continue;
			if(t > tmax)
				return false;

			tcross = t;
			rising = b;
			return true;
		}
		return false;
	}

	return false;
}

/**
	@brief Crops a waveform to a window of samples

	@param wfm		Waveform to crop
	@param start	Index of the first sample (in timebase units) to keep
	@param len		Length of the window
 */
void DemoOscilloscope::CropWaveform(WaveformBase* wfm, size_t start, size_t len)
{
	wfm->PrepareForCpuAccess();

	auto ua = dynamic_cast<UniformAnalogWaveform*>(wfm);
	if(ua)
	{
		size_t avail = ua->size();
		size_t n = 0;
		if(start < avail)
			n = min(len, avail - start);
		float* p = ua->m_samples.GetCpuPointer();
		memmove(p, p + start, n * sizeof(float));
		ua->Resize(n);
		ua->MarkModifiedFromCpu();
		return;
	}

	auto sd = dynamic_cast<SparseDigitalWaveform*>(wfm);
	if(sd)
	{
		//Keep samples overlapping the window, clipped to it
		int64_t wstart = start;
		int64_t wend = start + len;
		size_t j = 0;
		for(size_t i=0; i<sd->size(); i++)
		{
			int64_t off = sd->m_offsets[i];
			int64_t end = off + sd->m_durations[i];
			if(end <= wstart)
				continue;
			if(off >= wend)
				break;

			int64_t clippedStart = max(off, wstart);
			int64_t clippedEnd = min(end, wend);
			bool v = sd->m_samples[i];
			sd->m_offsets[j] = clippedStart - wstart;
			sd->m_durations[j] = clippedEnd - clippedStart;
			sd->m_samples[j] = v;
			j++;
		}
		sd->Resize(j);
		sd->MarkModifiedFromCpu();
	}
}

/**
	@brief Extends a digital waveform by repeating it

	@param wfm		Waveform to extend
	@param period	Repeat interval, in timebase units
	@param len		Total length to fill, in timebase units
 */
void DemoOscilloscope::RepeatWaveform(SparseDigitalWaveform* wfm, size_t period, size_t len)
{
	size_t n = wfm->size();
	if( (n == 0) || (period >= len) )
		return;

	//Stretch the last sample to the end of the period so the copies are contiguous
	wfm->PrepareForCpuAccess();
	wfm->m_durations[n-1] = period - wfm->m_offsets[n-1];

	size_t copies = (len + period - 1) / period;
	wfm->Resize(n * copies);
	for(size_t k=1; k<copies; k++)
	{
		for(size_t i=0; i<n; i++)
		{
			wfm->m_offsets[k*n + i] = wfm->m_offsets[i] + k*period;
			wfm->m_durations[k*n + i] = wfm->m_durations[i];
			wfm->m_samples[k*n + i] = wfm->m_samples[i];
		}
	}
	wfm->MarkModifiedFromCpu();
}
