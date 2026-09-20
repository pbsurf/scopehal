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
	@brief Implementation of DemoFunctionGenerator
 */

#include "scopehal.h"
#include "DemoFunctionGenerator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPINullTransport object (this driver does not connect to real hardware)
 */
DemoFunctionGenerator::DemoFunctionGenerator(SCPITransport* transport)
	: SCPIDevice(transport, false)
	, SCPIInstrument(transport, false)
{
	m_model = "Function Generator Simulator";
	m_vendor = "Entropic Engineering";
	m_serial = "12345";

	m_channels.push_back(new FunctionGeneratorChannel(this, "CH1", "#ffff00", 0));
	m_channels.push_back(new FunctionGeneratorChannel(this, "CH2", "#00ffff", 1));

	//Give the channels different settings so it's obvious which is which
	m_enabled[0] = true;
	m_amplitude[0] = 1;
	m_offset[0] = 0;
	m_frequency[0] = 1e3;
	m_shape[0] = SHAPE_SINE;
	m_dutyCycle[0] = 0.5;
	m_riseTime[0] = 1e6;
	m_fallTime[0] = 1e6;
	m_impedance[0] = IMPEDANCE_50_OHM;

	m_enabled[1] = false;
	m_amplitude[1] = 2;
	m_offset[1] = 0.5;
	m_frequency[1] = 10e3;
	m_shape[1] = SHAPE_SQUARE;
	m_dutyCycle[1] = 0.25;
	m_riseTime[1] = 10e6;
	m_fallTime[1] = 20e6;
	m_impedance[1] = IMPEDANCE_HIGH_Z;
}

DemoFunctionGenerator::~DemoFunctionGenerator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

///@brief Return the constant driver name string "demofuncgen"
string DemoFunctionGenerator::GetDriverNameInternal()
{
	return "demofuncgen";
}

unsigned int DemoFunctionGenerator::GetInstrumentTypes() const
{
	return INST_FUNCTION;
}

uint32_t DemoFunctionGenerator::GetInstrumentTypesForChannel(size_t i) const
{
	if(i < m_numChans)
		return INST_FUNCTION;
	else
		return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FunctionGenerator

vector<FunctionGenerator::WaveShape> DemoFunctionGenerator::GetAvailableWaveformShapes(int /*chan*/)
{
	//Everything we know how to name, except arbitrary waveforms (we have no way to upload one)
	return
	{
		SHAPE_SINE,
		SHAPE_SQUARE,
		SHAPE_TRIANGLE,
		SHAPE_PULSE,
		SHAPE_DC,
		SHAPE_NOISE,
		SHAPE_SAWTOOTH_UP,
		SHAPE_SAWTOOTH_DOWN,
		SHAPE_SINC,
		SHAPE_GAUSSIAN,
		SHAPE_LORENTZ,
		SHAPE_HALF_SINE,
		SHAPE_PRBS_NONSTANDARD,
		SHAPE_EXPONENTIAL_RISE,
		SHAPE_EXPONENTIAL_DECAY,
		SHAPE_HAVERSINE,
		SHAPE_CARDIAC,
		SHAPE_STAIRCASE_UP,
		SHAPE_STAIRCASE_DOWN,
		SHAPE_STAIRCASE_UP_DOWN,
		SHAPE_NEGATIVE_PULSE,
		SHAPE_LOG_RISE,
		SHAPE_LOG_DECAY,
		SHAPE_SQUARE_ROOT,
		SHAPE_CUBE_ROOT,
		SHAPE_QUADRATIC,
		SHAPE_CUBIC,
		SHAPE_DLORENTZ,
		SHAPE_GAUSSIAN_PULSE,
		SHAPE_HAMMING,
		SHAPE_HANNING,
		SHAPE_KAISER,
		SHAPE_BLACKMAN,
		SHAPE_GAUSSIAN_WINDOW,
		SHAPE_HARRIS,
		SHAPE_BARTLETT,
		SHAPE_TAN,
		SHAPE_COT,
		SHAPE_SEC,
		SHAPE_CSC,
		SHAPE_ASIN,
		SHAPE_ACOS,
		SHAPE_ATAN,
		SHAPE_ACOT
	};
}

bool DemoFunctionGenerator::GetFunctionChannelActive(int chan)
{
	return m_enabled[chan];
}

void DemoFunctionGenerator::SetFunctionChannelActive(int chan, bool on)
{
	m_enabled[chan] = on;
}

bool DemoFunctionGenerator::HasFunctionDutyCycleControls(int /*chan*/)
{
	return true;
}

float DemoFunctionGenerator::GetFunctionChannelDutyCycle(int chan)
{
	return m_dutyCycle[chan];
}

void DemoFunctionGenerator::SetFunctionChannelDutyCycle(int chan, float duty)
{
	m_dutyCycle[chan] = clamp(duty, 0.0f, 1.0f);
}

float DemoFunctionGenerator::GetFunctionChannelAmplitude(int chan)
{
	return m_amplitude[chan];
}

void DemoFunctionGenerator::SetFunctionChannelAmplitude(int chan, float amplitude)
{
	m_amplitude[chan] = clamp(amplitude, m_minAmplitude, m_maxAmplitude);
}

float DemoFunctionGenerator::GetFunctionChannelOffset(int chan)
{
	return m_offset[chan];
}

void DemoFunctionGenerator::SetFunctionChannelOffset(int chan, float offset)
{
	m_offset[chan] = clamp(offset, -m_maxOffset, m_maxOffset);
}

float DemoFunctionGenerator::GetFunctionChannelFrequency(int chan)
{
	return m_frequency[chan];
}

void DemoFunctionGenerator::SetFunctionChannelFrequency(int chan, float hz)
{
	m_frequency[chan] = clamp(hz, m_minFrequency, m_maxFrequency);
}

FunctionGenerator::WaveShape DemoFunctionGenerator::GetFunctionChannelShape(int chan)
{
	return m_shape[chan];
}

void DemoFunctionGenerator::SetFunctionChannelShape(int chan, WaveShape shape)
{
	//Ignore shapes we don't offer
	auto shapes = GetAvailableWaveformShapes(chan);
	if(find(shapes.begin(), shapes.end(), shape) != shapes.end())
		m_shape[chan] = shape;
}

bool DemoFunctionGenerator::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return true;
}

float DemoFunctionGenerator::GetFunctionChannelRiseTime(int chan)
{
	return m_riseTime[chan];
}

void DemoFunctionGenerator::SetFunctionChannelRiseTime(int chan, float fs)
{
	m_riseTime[chan] = clamp(fs, m_minEdgeTime, m_maxEdgeTime);
}

float DemoFunctionGenerator::GetFunctionChannelFallTime(int chan)
{
	return m_fallTime[chan];
}

void DemoFunctionGenerator::SetFunctionChannelFallTime(int chan, float fs)
{
	m_fallTime[chan] = clamp(fs, m_minEdgeTime, m_maxEdgeTime);
}

bool DemoFunctionGenerator::HasFunctionImpedanceControls(int /*chan*/)
{
	return true;
}

FunctionGenerator::OutputImpedance DemoFunctionGenerator::GetFunctionChannelOutputImpedance(int chan)
{
	return m_impedance[chan];
}

void DemoFunctionGenerator::SetFunctionChannelOutputImpedance(int chan, OutputImpedance z)
{
	m_impedance[chan] = z;
}
