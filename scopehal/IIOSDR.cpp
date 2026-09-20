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
	@brief Implementation of IIOSDR
	@ingroup sdrdrivers
 */

#ifdef HAS_IIO

#include "scopehal.h"
#include "ComplexChannel.h"
#include "IIOSDR.h"

using namespace std;

//Names of the IIO devices in the Linux ad9361 driver stack
static const char* g_phyDevice = "ad9361-phy";
static const char* g_rxDevice = "cf-ad9361-lpc";

//Known limits of the AD936x family, used to clamp requests. The hardware may be more restrictive
//(the AD9363 only goes from 325 MHz to 3.8 GHz, for example) in which case we read back what it actually did.
static const int64_t g_minCenterFreq = 70000000;
static const int64_t g_maxCenterFreq = 6000000000;
static const int64_t g_minBandwidth = 200000;
static const int64_t g_maxBandwidth = 56000000;
static const uint64_t g_minSampleRate = 2083334;
static const uint64_t g_maxSampleRate = 61440000;

//Full scale of the 12 bit ADC (sign extended into 16 bits by libiio)
static const float g_adcScale = 1.0f / 2048;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Constructs a new driver object

	@param transport	SCPIIIOTransport connected to an IIO context
 */
IIOSDR::IIOSDR(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_ctx(nullptr)
	, m_numRx(0)
	, m_centerFreq(2400000000)
	, m_span(2000000)
	, m_sampleRate(2500000)
	, m_sampleDepth(65536)
	, m_centerFreqDirty(false)
	, m_spanDirty(false)
	, m_sampleRateDirty(false)
	, m_hwCenterFreq(m_centerFreq)
	, m_hwSampleRate(m_sampleRate)
{
	auto iio = dynamic_cast<SCPIIIOTransport*>(transport);
	if(iio)
		m_ctx = iio->GetContext();
	if(!m_ctx)
	{
		LogError("IIOSDR requires an IIO transport with a valid context\n");
		return;
	}

	if(!m_ctx->HasDevice(g_phyDevice) || !m_ctx->HasDevice(g_rxDevice))
	{
		LogError("This IIO device does not look like an AD936x (no %s / %s), it is not supported\n",
			g_phyDevice, g_rxDevice);
		m_ctx = nullptr;
		return;
	}

	//Figure out how many receive paths we have. AD9363/AD9364 have one, AD9361 has two.
	while( (m_numRx < 2) && m_ctx->HasChannel(g_phyDevice, "voltage" + to_string(m_numRx), false) )
		m_numRx ++;

	for(size_t i=0; i<m_numRx; i++)
	{
		auto chan = new ComplexChannel(
			this,
			string("RX") + to_string(i+1),
			GetChannelColor(i),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			m_channels.size());
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();

		//Range and offset are purely client side, the ADC is always full scale
		SetChannelOffset(i, 0, 0);
		SetChannelOffset(i, 1, 0);
		SetChannelVoltageRange(i, 0, 2);
		SetChannelVoltageRange(i, 1, 2);
	}

	//Only the first channel is enabled by default
	m_channelEnabled.resize(m_numRx, false);
	if(m_numRx > 0)
		m_channelEnabled[0] = true;

	//Adopt whatever the radio is currently doing rather than stomping on it
	ReadHardwareConfiguration();
	LogDebug("IIO SDR has %zu receive path(s), LO %" PRId64 " Hz, rate %" PRIu64 " Hz, bandwidth %" PRId64 " Hz\n",
		m_numRx, m_centerFreq, m_sampleRate, m_span);
}

IIOSDR::~IIOSDR()
{
}

/**
	@brief Color the channels arbitrarily (yellow-cyan-magenta-green)

	@param i	Channel number
 */
string IIOSDR::GetChannelColor(size_t i)
{
	switch(i % 4)
	{
		case 0:
			return "#ffd700";

		case 1:
			return "#00bfff";

		case 2:
			return "#ff00ff";

		case 3:
		default:
			return "#00ff00";
	}
}

/**
	@brief Reads the current LO frequency, sample rate, and bandwidth from the hardware and adopts them
 */
void IIOSDR::ReadHardwareConfiguration()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	int64_t lo;
	int64_t rate;
	int64_t bw;
	if(m_ctx->ReadChannelAttrInt(g_phyDevice, "altvoltage0", true, "frequency", lo))
		m_centerFreq = lo;
	if(m_ctx->ReadChannelAttrInt(g_phyDevice, "voltage0", false, "sampling_frequency", rate))
		m_sampleRate = rate;
	if(m_ctx->ReadChannelAttrInt(g_phyDevice, "voltage0", false, "rf_bandwidth", bw))
		m_span = bw;

	m_hwCenterFreq = m_centerFreq;
	m_hwSampleRate = m_sampleRate;
	m_centerFreqDirty = false;
	m_spanDirty = false;
	m_sampleRateDirty = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configuration push (instrument thread)

void IIOSDR::BackgroundProcessing()
{
	SCPIInstrument::BackgroundProcessing();
	ApplyConfiguration();
}

/**
	@brief Pushes any changed settings to the hardware, then reads back what it actually did

	This is intentionally done here rather than in the setters so that we never talk to the radio from the GUI thread.
 */
void IIOSDR::ApplyConfiguration()
{
	if(!m_ctx)
		return;

	//Grab the pending changes
	bool doRate;
	bool doSpan;
	bool doFreq;
	int64_t freq;
	int64_t span;
	uint64_t rate;
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		doRate = m_sampleRateDirty;
		doSpan = m_spanDirty;
		doFreq = m_centerFreqDirty;
		freq = m_centerFreq;
		span = m_span;
		rate = m_sampleRate;
		m_sampleRateDirty = false;
		m_spanDirty = false;
		m_centerFreqDirty = false;
	}

	if(!doRate && !doSpan && !doFreq)
		return;

	//Changing the sample rate can change the analog bandwidth, so do it first
	//(failures are logged by the context, and we resync from the hardware below)
	if(doRate)
		m_ctx->WriteChannelAttrInt(g_phyDevice, "voltage0", false, "sampling_frequency", rate);
	if(doSpan)
		m_ctx->WriteChannelAttrInt(g_phyDevice, "voltage0", false, "rf_bandwidth", span);
	if(doFreq)
		m_ctx->WriteChannelAttrInt(g_phyDevice, "altvoltage0", true, "frequency", freq);

	//Read back what we actually got. If the user changed something again while we were busy, leave it for next time.
	int64_t hwFreq;
	int64_t hwSpan;
	int64_t hwRate;
	bool haveFreq = m_ctx->ReadChannelAttrInt(g_phyDevice, "altvoltage0", true, "frequency", hwFreq);
	bool haveSpan = m_ctx->ReadChannelAttrInt(g_phyDevice, "voltage0", false, "rf_bandwidth", hwSpan);
	bool haveRate = m_ctx->ReadChannelAttrInt(g_phyDevice, "voltage0", false, "sampling_frequency", hwRate);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(haveFreq)
	{
		m_hwCenterFreq = hwFreq;
		if(!m_centerFreqDirty)
			m_centerFreq = hwFreq;
	}
	if(haveSpan && !m_spanDirty)
		m_span = hwSpan;
	if(haveRate)
	{
		m_hwSampleRate = hwRate;
		if(!m_sampleRateDirty)
			m_sampleRate = hwRate;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Return the constant driver name string "iio"
string IIOSDR::GetDriverNameInternal()
{
	return "iio";
}

bool IIOSDR::HasTimebaseControls()
{
	return true;
}

bool IIOSDR::HasFrequencyControls()
{
	return true;
}

bool IIOSDR::HasResolutionBandwidth()
{
	return false;
}

bool IIOSDR::CanInterleave()
{
	return false;
}

OscilloscopeChannel* IIOSDR::GetExternalTrigger()
{
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool IIOSDR::IsChannelEnabled(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return (i < m_channelEnabled.size()) && m_channelEnabled[i];
}

void IIOSDR::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(i < m_channelEnabled.size())
		m_channelEnabled[i] = true;
}

void IIOSDR::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(i < m_channelEnabled.size())
		m_channelEnabled[i] = false;
}

OscilloscopeChannel::CouplingType IIOSDR::GetChannelCoupling(size_t /*i*/)
{
	//RF input, 50 ohms
	return OscilloscopeChannel::COUPLE_AC_50;
}

vector<OscilloscopeChannel::CouplingType> IIOSDR::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timebase and LO configuration

uint64_t IIOSDR::GetSampleRate()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_sampleRate;
}

void IIOSDR::SetSampleRate(uint64_t rate)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_sampleRate = min(max(rate, g_minSampleRate), g_maxSampleRate);
	m_sampleRateDirty = true;
}

uint64_t IIOSDR::GetSampleDepth()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_sampleDepth;
}

void IIOSDR::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_sampleDepth = depth;
}

vector<uint64_t> IIOSDR::GetSampleRatesNonInterleaved()
{
	//Common rates that the AD936x clock chain can generate
	return
	{
		2500000,
		3000000,
		4000000,
		5000000,
		6000000,
		8000000,
		10000000,
		12000000,
		15000000,
		20000000,
		25000000,
		30720000,
		40000000,
		50000000,
		61440000
	};
}

vector<uint64_t> IIOSDR::GetSampleDepthsNonInterleaved()
{
	return { 1024, 4096, 16384, 65536, 262144, 1048576 };
}

void IIOSDR::SetSpan(int64_t span)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_span = min(max(span, g_minBandwidth), g_maxBandwidth);
	m_spanDirty = true;
}

int64_t IIOSDR::GetSpan()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_span;
}

void IIOSDR::SetCenterFrequency(size_t /*channel*/, int64_t freq)
{
	//There's only one RX LO, shared by all receive paths
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_centerFreq = min(max(freq, g_minCenterFreq), g_maxCenterFreq);
	m_centerFreqDirty = true;
}

int64_t IIOSDR::GetCenterFrequency(size_t /*channel*/)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_centerFreq;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

void IIOSDR::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void IIOSDR::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void IIOSDR::Stop()
{
	m_triggerArmed = false;
}

void IIOSDR::ForceTrigger()
{
	//No trigger, so this is the same as a single capture
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool IIOSDR::IsTriggerArmed()
{
	return m_triggerArmed;
}

void IIOSDR::PushTrigger()
{
	//no triggering
}

void IIOSDR::PullTrigger()
{
	//no triggering
}

Oscilloscope::TriggerMode IIOSDR::PollTrigger()
{
	if(!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	//If there's nothing to capture, don't spin
	bool anyEnabled = false;
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		for(bool e : m_channelEnabled)
			anyEnabled |= e;
	}
	if(!m_ctx || !anyEnabled)
	{
		this_thread::sleep_for(chrono::milliseconds(5));
		return TRIGGER_MODE_RUN;
	}

	//There's no trigger, we always capture as soon as we're armed. Report "triggered" so that we block in
	//AcquireData() until the samples arrive.
	return TRIGGER_MODE_TRIGGERED;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Waveform acquisition

bool IIOSDR::AcquireData()
{
	if(!m_ctx)
		return false;

	//Snapshot what we're capturing. Channels come in I/Q pairs (voltage0 = RX1 I, voltage1 = RX1 Q, ...)
	vector<size_t> paths;
	vector<string> iioChannels;
	size_t depth;
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		depth = m_sampleDepth;
		for(size_t i=0; i<m_numRx; i++)
		{
			if(!m_channelEnabled[i])
				continue;
			paths.push_back(i);
			iioChannels.push_back("voltage" + to_string(i*2));
			iioChannels.push_back("voltage" + to_string(i*2 + 1));
		}
	}
	if(paths.empty())
		return false;

	//Sample rate and LO are not necessarily what the user last asked for, they're what's in the hardware now.
	//(only this thread applies configuration, so these can't change during the capture)
	double now = GetTime();
	int64_t centerFreq = m_hwCenterFreq;
	int64_t fs_per_sample = FS_PER_SECOND / m_hwSampleRate;

	vector<vector<int16_t> > data;
	if(!m_ctx->CaptureBlock(g_rxDevice, iioChannels, depth, data))
	{
		//Don't spin on a failing capture, stop and let the user sort it out
		LogError("IIO capture failed, stopping acquisition\n");
		m_triggerArmed = false;
		return false;
	}

	SequenceSet s;
	for(size_t n=0; n<paths.size(); n++)
	{
		size_t i = paths[n];
		auto& idata = data[n*2];
		auto& qdata = data[n*2 + 1];

		string base = m_nickname + "." + GetOscilloscopeChannel(i)->GetHwname();
		auto icap = AllocateAnalogWaveform(base + ".i");
		icap->m_timescale = fs_per_sample;
		icap->m_triggerPhase = 0;
		icap->m_startTimestamp = floor(now);
		icap->m_startFemtoseconds = (now - floor(now)) * FS_PER_SECOND;
		icap->Resize(depth);

		auto qcap = AllocateAnalogWaveform(base + ".q");
		qcap->m_timescale = fs_per_sample;
		qcap->m_triggerPhase = 0;
		qcap->m_startTimestamp = floor(now);
		qcap->m_startFemtoseconds = (now - floor(now)) * FS_PER_SECOND;
		qcap->Resize(depth);

		//Convert to floating point, normalized to full scale = +/- 1
		icap->PrepareForCpuAccess();
		qcap->PrepareForCpuAccess();
		for(size_t j=0; j<depth; j++)
		{
			icap->m_samples[j] = idata[j] * g_adcScale;
			qcap->m_samples[j] = qdata[j] * g_adcScale;
		}
		icap->MarkSamplesModifiedFromCpu();
		qcap->MarkSamplesModifiedFromCpu();

		s[StreamDescriptor(GetChannel(i), 0)] = icap;
		s[StreamDescriptor(GetChannel(i), 1)] = qcap;

		dynamic_cast<ComplexChannel*>(GetChannel(i))->UpdateCenterFrequency(centerFreq);
	}

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

#endif
