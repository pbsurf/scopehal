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
	@brief Declaration of DemoFunctionGenerator
 */

#ifndef DemoFunctionGenerator_h
#define DemoFunctionGenerator_h

/**
	@brief A simulated function generator for demonstration and testing of the user interface

	Two channels, both of which support every optional control (duty cycle, rise/fall time, output impedance).
	There is no hardware behind this driver: settings are simply stored and read back, subject to the same sort of
	range limits a real instrument would have.
 */
class DemoFunctionGenerator : public virtual SCPIFunctionGenerator
{
public:
	DemoFunctionGenerator(SCPITransport* transport);
	virtual ~DemoFunctionGenerator();

	//Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan) override;

	virtual bool GetFunctionChannelActive(int chan) override;
	virtual void SetFunctionChannelActive(int chan, bool on) override;

	virtual bool HasFunctionDutyCycleControls(int chan) override;
	virtual float GetFunctionChannelDutyCycle(int chan) override;
	virtual void SetFunctionChannelDutyCycle(int chan, float duty) override;

	virtual float GetFunctionChannelAmplitude(int chan) override;
	virtual void SetFunctionChannelAmplitude(int chan, float amplitude) override;

	virtual float GetFunctionChannelOffset(int chan) override;
	virtual void SetFunctionChannelOffset(int chan, float offset) override;

	virtual float GetFunctionChannelFrequency(int chan) override;
	virtual void SetFunctionChannelFrequency(int chan, float hz) override;

	virtual WaveShape GetFunctionChannelShape(int chan) override;
	virtual void SetFunctionChannelShape(int chan, WaveShape shape) override;

	virtual bool HasFunctionRiseFallTimeControls(int chan) override;
	virtual float GetFunctionChannelRiseTime(int chan) override;
	virtual void SetFunctionChannelRiseTime(int chan, float fs) override;
	virtual float GetFunctionChannelFallTime(int chan) override;
	virtual void SetFunctionChannelFallTime(int chan, float fs) override;

	virtual bool HasFunctionImpedanceControls(int chan) override;
	virtual OutputImpedance GetFunctionChannelOutputImpedance(int chan) override;
	virtual void SetFunctionChannelOutputImpedance(int chan, OutputImpedance z) override;

protected:
	static constexpr int m_numChans = 2;

	//Limits, like a real instrument would have
	static constexpr float m_minFrequency = 1e-6;		//Hz
	static constexpr float m_maxFrequency = 25e6;		//Hz
	static constexpr float m_minAmplitude = 1e-3;		//Vpp
	static constexpr float m_maxAmplitude = 20;			//Vpp
	static constexpr float m_maxOffset = 10;			//V
	static constexpr float m_minEdgeTime = 1e6;			//fs (1 ns)
	static constexpr float m_maxEdgeTime = 1e12;		//fs (1 ms)

	bool m_enabled[m_numChans];
	float m_amplitude[m_numChans];
	float m_offset[m_numChans];
	float m_frequency[m_numChans];
	WaveShape m_shape[m_numChans];
	float m_dutyCycle[m_numChans];
	float m_riseTime[m_numChans];
	float m_fallTime[m_numChans];
	OutputImpedance m_impedance[m_numChans];

public:
	static std::string GetDriverNameInternal();

	//This is intentionally not virtual since it's a static method used by enumeration
	//cppcheck-suppress duplInheritedMember
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
		{"Demo Function Generator", {{ SCPITransportType::TRANSPORT_NULL, "" }}}
		};
	}
	GENERATOR_INITPROC(DemoFunctionGenerator)
};

#endif
