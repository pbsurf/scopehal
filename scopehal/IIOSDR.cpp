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

#include <sstream>

#include "scopehal.h"
#include "ComplexChannel.h"
#include "IIOSDR.h"

using namespace std;

//Names of the IIO devices in the Linux ad9361 driver stack
static const char* g_phyDevice = "ad9361-phy";
static const char* g_rxDevice = "cf-ad9361-lpc";
static const char* g_ddsDevice = "cf-ad9361-dds-core-lpc";

//Maximum number of transmit paths (AD9361) and tones per path (each is a complex tone made from a pair of DDSs)
static const size_t g_maxTx = 2;
static const size_t g_maxTones = 2;

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
	, m_numTx(0)
	, m_numTones(0)
	, m_txLoFreq(2400000000)
	, m_txLoDirty(false)
	, m_txLoMin(70000000)
	, m_txLoMax(6000000000)
	, m_txMaxToneFreq(0)
	, m_hwCenterFreq(m_centerFreq)
	, m_hwSampleRate(m_sampleRate)
{
	//Conservative limits until we've looked at the radio
	m_limits = { 70000000, 6000000000, 200000, 56000000, 2083334, 61440000, -3, 71 };

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

	m_gain.resize(m_numRx, 0);
	m_gainMode.resize(m_numRx);
	m_gainDirty.resize(m_numRx, false);
	m_gainModeDirty.resize(m_numRx, false);

	DetectLimits();
	DetectTransmitter();

	//Adopt whatever the radio is currently doing rather than stomping on it
	ReadHardwareConfiguration();
	LogDebug("IIO SDR has %zu receive path(s), LO %" PRId64 " Hz, rate %" PRIu64 " Hz, bandwidth %" PRId64 " Hz\n",
		m_numRx, m_centerFreq, m_sampleRate, m_span);
	if(m_numTx > 0)
		LogDebug("IIO SDR has %zu transmit path(s) with %zu tone(s) each, LO %" PRId64 " Hz\n",
			m_numTx, m_numTones, m_txLoFreq);
	LogDebug("Limits: LO %" PRId64 " - %" PRId64 " Hz, bandwidth %" PRId64 " - %" PRId64 " Hz, rate %" PRIu64 " - %" PRIu64
		" Hz, gain %.0f - %.0f dB\n",
		m_limits.minCenterFreq, m_limits.maxCenterFreq, m_limits.minBandwidth, m_limits.maxBandwidth,
		m_limits.minSampleRate, m_limits.maxSampleRate, m_limits.minGain, m_limits.maxGain);
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
	@brief Reads an IIO range attribute of the form "[min step max]"

	@param min		Minimum value
	@param max		Maximum value

	@return		True if the attribute exists and could be parsed
 */
static bool ReadRange(
	IIOContext* ctx, const char* dev, const char* chan, bool output, const string& attr, double& min, double& max)
{
	string tmp;
	if(!ctx->HasChannelAttr(dev, chan, output, attr))
		return false;
	if(!ctx->ReadChannelAttr(dev, chan, output, attr, tmp))
		return false;

	double step;
	return (3 == sscanf(tmp.c_str(), "[%lf %lf %lf]", &min, &step, &max));
}

/**
	@brief Figures out what this radio can do

	We start with defaults for the chip in the radio (the AD9363 in a stock PlutoSDR can't tune as far as an AD9361),
	then use the ranges published by the driver if they are available since these are authoritative and reflect any
	firmware unlocking.
 */
void IIOSDR::DetectLimits()
{
	//Everything we understand is the same as the AD9361 other than the AD9363
	auto model = m_ctx->GetAttributes()["hw_model"];
	if(model.find("AD9363") != string::npos)
	{
		m_limits.minCenterFreq = 325000000;
		m_limits.maxCenterFreq = 3800000000;
		m_limits.maxBandwidth = 20000000;
	}

	double lo;
	double hi;
	if(ReadRange(m_ctx, g_phyDevice, "altvoltage0", true, "frequency_available", lo, hi))
	{
		m_limits.minCenterFreq = lo;
		m_limits.maxCenterFreq = hi;
	}
	if(ReadRange(m_ctx, g_phyDevice, "voltage0", false, "rf_bandwidth_available", lo, hi))
	{
		m_limits.minBandwidth = lo;
		m_limits.maxBandwidth = hi;
	}
	if(ReadRange(m_ctx, g_phyDevice, "voltage0", false, "sampling_frequency_available", lo, hi))
	{
		m_limits.minSampleRate = lo;
		m_limits.maxSampleRate = hi;
	}
	if(ReadRange(m_ctx, g_phyDevice, "voltage0", false, "hardwaregain_available", lo, hi))
	{
		m_limits.minGain = lo;
		m_limits.maxGain = hi;
	}

	//Gain control modes, in the order the driver lists them
	m_gainModes.clear();
	string modes;
	if(m_ctx->HasChannelAttr(g_phyDevice, "voltage0", false, "gain_control_mode_available") &&
		m_ctx->ReadChannelAttr(g_phyDevice, "voltage0", false, "gain_control_mode_available", modes))
	{
		stringstream ss(modes);
		string mode;
		while(ss >> mode)
			m_gainModes.push_back(mode);
	}
	if(m_gainModes.empty())
		m_gainModes = { "manual", "slow_attack", "fast_attack", "hybrid" };
}

/**
	@brief Figures out whether we can transmit

	Transmitting is done with the DDS core in the FPGA, which is what the stock firmware of the radios we support has.
	Each transmit path has one DDS per tone for each of I and Q, named TXn_I_Fm and TXn_Q_Fm. A tone is a complex
	sinusoid, made by running the I and Q DDSs at the same frequency 90 degrees apart.
 */
void IIOSDR::DetectTransmitter()
{
	m_numTx = 0;
	m_numTones = 0;
	m_txTones.clear();

	if(!m_ctx->HasDevice(g_ddsDevice))
		return;

	while( (m_numTones < g_maxTones) && m_ctx->HasChannel(g_ddsDevice, GetToneChannelName(0, m_numTones, false), true) )
		m_numTones ++;
	while( (m_numTx < g_maxTx) &&
		m_ctx->HasChannel(g_ddsDevice, GetToneChannelName(m_numTx, 0, false), true) &&
		m_ctx->HasChannel(g_ddsDevice, GetToneChannelName(m_numTx, 0, true), true) &&
		m_ctx->HasChannel(g_phyDevice, "voltage" + to_string(m_numTx), true) )
	{
		m_numTx ++;
	}
	if( (m_numTx == 0) || (m_numTones == 0) )
	{
		m_numTx = 0;
		m_numTones = 0;
		return;
	}

	TxTone off = { false, 0, 0, false };
	m_txTones.assign(m_numTx, vector<TxTone>(m_numTones, off));

	for(size_t i=0; i<m_numTx; i++)
	{
		auto chan = new SDRTransmitChannel(
			this,
			string("TX") + to_string(i+1),
			GetChannelColor(m_numRx + i),
			m_channels.size(),
			i);
		m_channels.push_back(chan);
	}

	//The TX LO usually has the same range as the RX LO, but use what the radio says if it says something
	m_txLoMin = m_limits.minCenterFreq;
	m_txLoMax = m_limits.maxCenterFreq;
	double lo;
	double hi;
	if(ReadRange(m_ctx, g_phyDevice, "altvoltage1", true, "frequency_available", lo, hi))
	{
		m_txLoMin = lo;
		m_txLoMax = hi;
	}
	m_txMaxToneFreq = m_sampleRate / 2;
}

/**
	@brief Gets the IIO name of one of the DDS channels making up a tone

	@param tx		Zero-based transmit path
	@param tone		Zero-based tone
	@param q		True for the Q DDS, false for the I DDS
 */
string IIOSDR::GetToneChannelName(size_t tx, size_t tone, bool q)
{
	return "TX" + to_string(tx+1) + (q ? "_Q_F" : "_I_F") + to_string(tone+1);
}

/**
	@brief Reads the state of a tone from the hardware

	@return		True if the tone could be read
 */
bool IIOSDR::ReadTone(size_t tx, size_t tone, TxTone& out)
{
	auto iname = GetToneChannelName(tx, tone, false);
	auto qname = GetToneChannelName(tx, tone, true);

	int64_t freq;
	int64_t raw;
	int64_t iphase;
	int64_t qphase;
	double scale;
	if( !m_ctx->ReadChannelAttrInt(g_ddsDevice, iname, true, "frequency", freq) ||
		!m_ctx->ReadChannelAttrInt(g_ddsDevice, iname, true, "raw", raw) ||
		!m_ctx->ReadChannelAttrDouble(g_ddsDevice, iname, true, "scale", scale) ||
		!m_ctx->ReadChannelAttrInt(g_ddsDevice, iname, true, "phase", iphase) ||
		!m_ctx->ReadChannelAttrInt(g_ddsDevice, qname, true, "phase", qphase) )
	{
		return false;
	}

	//Positive frequencies have I leading Q by 90 degrees (phases are in millidegrees), negative ones have Q leading
	int64_t diff = ((iphase - qphase) % 360000 + 360000) % 360000;
	bool negative = (diff > 180000);

	out.enabled = (raw != 0);
	out.freq = negative ? -freq : freq;
	out.amplitude = scale;
	return true;
}

/**
	@brief Writes the state of a tone to the hardware

	Failures are logged by the context.
 */
void IIOSDR::WriteTone(size_t tx, size_t tone, const TxTone& in)
{
	int64_t freq = llabs(in.freq);
	bool negative = (in.freq < 0);

	for(bool q : { false, true })
	{
		auto name = GetToneChannelName(tx, tone, q);

		//The leading DDS is 90 degrees ahead
		bool leads = (q == negative);
		m_ctx->WriteChannelAttrInt(g_ddsDevice, name, true, "phase", leads ? 90000 : 0);
		m_ctx->WriteChannelAttrInt(g_ddsDevice, name, true, "frequency", freq);
		m_ctx->WriteChannelAttrDouble(g_ddsDevice, name, true, "scale", in.amplitude);
		m_ctx->WriteChannelAttrInt(g_ddsDevice, name, true, "raw", in.enabled ? 1 : 0);
	}
}

/**
	@brief Reads the current LO frequency, sample rate, bandwidth, and gain from the hardware and adopts them
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

	for(size_t i=0; i<m_numRx; i++)
	{
		string id = "voltage" + to_string(i);
		string mode;
		double gain;
		if(m_ctx->ReadChannelAttr(g_phyDevice, id, false, "gain_control_mode", mode))
			m_gainMode[i] = mode;
		if(m_ctx->ReadChannelAttrDouble(g_phyDevice, id, false, "hardwaregain", gain))
			m_gain[i] = gain;
		m_gainDirty[i] = false;
		m_gainModeDirty[i] = false;
	}

	if(m_numTx > 0)
	{
		int64_t txlo;
		if(m_ctx->ReadChannelAttrInt(g_phyDevice, "altvoltage1", true, "frequency", txlo))
			m_txLoFreq = txlo;
		m_txLoDirty = false;

		int64_t txrate;
		if(m_ctx->ReadChannelAttrInt(g_phyDevice, "voltage0", true, "sampling_frequency", txrate))
			m_txMaxToneFreq = txrate / 2;
		else
			m_txMaxToneFreq = m_sampleRate / 2;

		for(size_t i=0; i<m_numTx; i++)
		{
			for(size_t j=0; j<m_numTones; j++)
			{
				TxTone tone;
				if(ReadTone(i, j, tone))
				{
					m_txTones[i][j] = tone;
					m_txTones[i][j].dirty = false;
				}
			}
		}
	}
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
	vector<string> gainMode(m_numRx);
	vector<float> gain(m_numRx);
	vector<bool> doGainMode(m_numRx);
	vector<bool> doGain(m_numRx);
	bool doTxLo = false;
	int64_t txLo = 0;
	vector<pair<size_t, size_t> > txPending;
	vector<TxTone> txPendingTones;
	bool any;
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

		any = doRate || doSpan || doFreq;
		for(size_t i=0; i<m_numRx; i++)
		{
			gainMode[i] = m_gainMode[i];
			gain[i] = m_gain[i];

			doGainMode[i] = m_gainModeDirty[i];
			m_gainModeDirty[i] = false;

			//The gain can only be set in manual mode. If it's not, leave it pending until we get there.
			doGain[i] = m_gainDirty[i] && (gainMode[i] == "manual");
			if(doGain[i])
				m_gainDirty[i] = false;

			any |= doGainMode[i] || doGain[i];
		}

		doTxLo = m_txLoDirty;
		txLo = m_txLoFreq;
		m_txLoDirty = false;
		any |= doTxLo;

		for(size_t i=0; i<m_numTx; i++)
		{
			for(size_t j=0; j<m_numTones; j++)
			{
				if(!m_txTones[i][j].dirty)
					continue;

				txPending.push_back(pair<size_t, size_t>(i, j));
				txPendingTones.push_back(m_txTones[i][j]);
				m_txTones[i][j].dirty = false;
				any = true;
			}
		}
	}

	if(!any)
		return;

	//Changing the sample rate can change the analog bandwidth, so do it first
	//(failures are logged by the context, and we resync from the hardware below)
	if(doRate)
		m_ctx->WriteChannelAttrInt(g_phyDevice, "voltage0", false, "sampling_frequency", rate);
	if(doSpan)
		m_ctx->WriteChannelAttrInt(g_phyDevice, "voltage0", false, "rf_bandwidth", span);
	if(doFreq)
		m_ctx->WriteChannelAttrInt(g_phyDevice, "altvoltage0", true, "frequency", freq);
	if(doTxLo)
		m_ctx->WriteChannelAttrInt(g_phyDevice, "altvoltage1", true, "frequency", txLo);

	//The transmit sample rate follows the receive rate, and tones can't be faster than half of it
	int64_t maxTone = 0;
	if(m_numTx > 0)
	{
		int64_t txRate;
		if(m_ctx->ReadChannelAttrInt(g_phyDevice, "voltage0", true, "sampling_frequency", txRate))
			maxTone = txRate / 2;
		else
			maxTone = m_hwSampleRate / 2;
	}
	for(size_t n=0; n<txPending.size(); n++)
	{
		auto& tone = txPendingTones[n];
		tone.freq = min(max(tone.freq, -maxTone), maxTone);
		WriteTone(txPending[n].first, txPending[n].second, tone);
	}

	//Mode has to be set before gain
	for(size_t i=0; i<m_numRx; i++)
	{
		string id = "voltage" + to_string(i);
		if(doGainMode[i])
			m_ctx->WriteChannelAttr(g_phyDevice, id, false, "gain_control_mode", gainMode[i]);
		if(doGain[i])
			m_ctx->WriteChannelAttrDouble(g_phyDevice, id, false, "hardwaregain", gain[i]);
	}

	//Read back what we actually got. If the user changed something again while we were busy, leave it for next time.
	int64_t hwFreq;
	int64_t hwSpan;
	int64_t hwRate;
	bool haveFreq = m_ctx->ReadChannelAttrInt(g_phyDevice, "altvoltage0", true, "frequency", hwFreq);
	bool haveSpan = m_ctx->ReadChannelAttrInt(g_phyDevice, "voltage0", false, "rf_bandwidth", hwSpan);
	bool haveRate = m_ctx->ReadChannelAttrInt(g_phyDevice, "voltage0", false, "sampling_frequency", hwRate);

	vector<string> hwGainMode(m_numRx);
	vector<double> hwGain(m_numRx);
	vector<bool> haveGainMode(m_numRx);
	vector<bool> haveGain(m_numRx);
	for(size_t i=0; i<m_numRx; i++)
	{
		string id = "voltage" + to_string(i);
		haveGainMode[i] = m_ctx->ReadChannelAttr(g_phyDevice, id, false, "gain_control_mode", hwGainMode[i]);
		haveGain[i] = m_ctx->ReadChannelAttrDouble(g_phyDevice, id, false, "hardwaregain", hwGain[i]);
	}

	int64_t hwTxLo = 0;
	bool haveTxLo = (m_numTx > 0) && m_ctx->ReadChannelAttrInt(g_phyDevice, "altvoltage1", true, "frequency", hwTxLo);
	vector<TxTone> hwTones(txPending.size());
	vector<bool> haveTones(txPending.size());
	for(size_t n=0; n<txPending.size(); n++)
		haveTones[n] = ReadTone(txPending[n].first, txPending[n].second, hwTones[n]);

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
	for(size_t i=0; i<m_numRx; i++)
	{
		if(haveGainMode[i] && !m_gainModeDirty[i])
			m_gainMode[i] = hwGainMode[i];

		//If we still owe the radio a gain (waiting for manual mode) don't clobber it with what's there now
		if(haveGain[i] && !m_gainDirty[i])
			m_gain[i] = hwGain[i];
	}

	if(m_numTx > 0)
	{
		m_txMaxToneFreq = maxTone;
		if(haveTxLo && !m_txLoDirty)
			m_txLoFreq = hwTxLo;

		//The DDS may not be able to do exactly what we asked for
		for(size_t n=0; n<txPending.size(); n++)
		{
			auto& tone = m_txTones[txPending[n].first][txPending[n].second];
			if(haveTones[n] && !tone.dirty)
			{
				tone = hwTones[n];
				tone.dirty = false;
			}
		}
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
// RX gain

bool IIOSDR::HasGainControl(size_t i)
{
	return m_ctx && (i < m_numRx);
}

vector<string> IIOSDR::GetGainModes(size_t i)
{
	if(!HasGainControl(i))
		return vector<string>();
	return m_gainModes;
}

string IIOSDR::GetGainMode(size_t i)
{
	if(!HasGainControl(i))
		return "";

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_gainMode[i];
}

void IIOSDR::SetGainMode(size_t i, const string& mode)
{
	if(!HasGainControl(i))
		return;

	if(find(m_gainModes.begin(), m_gainModes.end(), mode) == m_gainModes.end())
	{
		LogWarning("Unsupported IIO gain control mode \"%s\"\n", mode.c_str());
		return;
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_gainMode[i] = mode;
	m_gainModeDirty[i] = true;
}

bool IIOSDR::IsGainAdjustable(size_t i)
{
	return HasGainControl(i) && (GetGainMode(i) == "manual");
}

pair<float, float> IIOSDR::GetGainRange(size_t /*i*/)
{
	return pair<float, float>(m_limits.minGain, m_limits.maxGain);
}

float IIOSDR::GetGain(size_t i)
{
	if(!HasGainControl(i))
		return 0;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_gain[i];
}

void IIOSDR::SetGain(size_t i, float gain)
{
	if(!HasGainControl(i))
		return;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_gain[i] = min(max(gain, m_limits.minGain), m_limits.maxGain);
	m_gainDirty[i] = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Transmit control

size_t IIOSDR::GetTxChannelCount()
{
	return m_numTx;
}

size_t IIOSDR::GetTxToneCount(size_t tx)
{
	if(tx >= m_numTx)
		return 0;
	return m_numTones;
}

int64_t IIOSDR::GetTxLOFrequency()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_txLoFreq;
}

void IIOSDR::SetTxLOFrequency(int64_t freq)
{
	if(m_numTx == 0)
		return;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_txLoFreq = min(max(freq, m_txLoMin), m_txLoMax);
	m_txLoDirty = true;
}

pair<int64_t, int64_t> IIOSDR::GetTxLOFrequencyRange()
{
	return pair<int64_t, int64_t>(m_txLoMin, m_txLoMax);
}

bool IIOSDR::IsTxToneEnabled(size_t tx, size_t tone)
{
	if( (tx >= m_numTx) || (tone >= m_numTones) )
		return false;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_txTones[tx][tone].enabled;
}

void IIOSDR::SetTxToneEnabled(size_t tx, size_t tone, bool enabled)
{
	if( (tx >= m_numTx) || (tone >= m_numTones) )
		return;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_txTones[tx][tone].enabled = enabled;
	m_txTones[tx][tone].dirty = true;
}

int64_t IIOSDR::GetTxToneFrequency(size_t tx, size_t tone)
{
	if( (tx >= m_numTx) || (tone >= m_numTones) )
		return 0;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_txTones[tx][tone].freq;
}

void IIOSDR::SetTxToneFrequency(size_t tx, size_t tone, int64_t freq)
{
	if( (tx >= m_numTx) || (tone >= m_numTones) )
		return;

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//The transmit rate follows the receive rate, so if that's about to change use the new one
	int64_t limit = m_sampleRateDirty ? static_cast<int64_t>(m_sampleRate / 2) : m_txMaxToneFreq;
	m_txTones[tx][tone].freq = min(max(freq, -limit), limit);
	m_txTones[tx][tone].dirty = true;
}

pair<int64_t, int64_t> IIOSDR::GetTxToneFrequencyRange(size_t /*tx*/)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	int64_t limit = m_sampleRateDirty ? static_cast<int64_t>(m_sampleRate / 2) : m_txMaxToneFreq;
	return pair<int64_t, int64_t>(-limit, limit);
}

float IIOSDR::GetTxToneAmplitude(size_t tx, size_t tone)
{
	if( (tx >= m_numTx) || (tone >= m_numTones) )
		return 0;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_txTones[tx][tone].amplitude;
}

void IIOSDR::SetTxToneAmplitude(size_t tx, size_t tone, float amplitude)
{
	if( (tx >= m_numTx) || (tone >= m_numTones) )
		return;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_txTones[tx][tone].amplitude = min(max(amplitude, 0.0f), 1.0f);
	m_txTones[tx][tone].dirty = true;
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
	m_sampleRate = min(max(rate, m_limits.minSampleRate), m_limits.maxSampleRate);
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
	static const uint64_t rates[] =
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

	vector<uint64_t> ret;
	for(auto r : rates)
	{
		if( (r >= m_limits.minSampleRate) && (r <= m_limits.maxSampleRate) )
			ret.push_back(r);
	}
	return ret;
}

vector<uint64_t> IIOSDR::GetSampleDepthsNonInterleaved()
{
	return { 1024, 4096, 16384, 65536, 262144, 1048576 };
}

void IIOSDR::SetSpan(int64_t span)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_span = min(max(span, m_limits.minBandwidth), m_limits.maxBandwidth);
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
	m_centerFreq = min(max(freq, m_limits.minCenterFreq), m_limits.maxCenterFreq);
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
