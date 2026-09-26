/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "../scopehal/scopehal.h"
#include "ComplexFFTFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ComplexFFTFilter::ComplexFFTFilter(const string& color)
	: FFTFilter(color)
{
	//remove base class ports
	m_inputs.clear();

	//Set up channels
	CreateInput<InputConstraintStreamType>("I", Stream::STREAM_TYPE_ANALOG);
	CreateInput<InputConstraintStreamType>("Q", Stream::STREAM_TYPE_ANALOG);
	CreateInput<InputConstraintAND>(
		"center",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintYUnit>(this, Unit(Unit::UNIT_HZ)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_ANALOG_SCALAR)
		});

	//Switch the window functions and postprocessing over to the complex versions
	m_blackmanHarrisComputePipeline.Reinitialize(
		"shaders/ComplexBlackmanHarrisWindow.spv", 3, sizeof(WindowFunctionArgs));
	m_rectangularComputePipeline.Reinitialize(
		"shaders/ComplexRectangularWindow.spv", 3, sizeof(WindowFunctionArgs));
	m_cosineSumComputePipeline.Reinitialize(
		"shaders/ComplexCosineSumWindow.spv", 3, sizeof(WindowFunctionArgs));
	m_complexToLogMagnitudeComputePipeline.Reinitialize(
		"shaders/ComplexToLogMagnitudeShifted.spv", 2, sizeof(ComplexToMagnitudeArgs));
}

ComplexFFTFilter::~ComplexFFTFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

//This is intentionally not virtual since it's a static method used by enumeration
//cppcheck-suppress duplInheritedMember
string ComplexFFTFilter::GetProtocolName()
{
	return "Complex FFT";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ComplexFFTFilter::ReallocateComplexBuffers(size_t npoints)
{
	m_cachedNumPoints = npoints;
	m_cachedNumPointsFFT = npoints;

	m_rdinbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_rdinbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_rdoutbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_rdoutbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	//Update our FFT plan if it's out of date
	if(m_vkPlan)
	{
		if(m_vkPlan->size() != npoints)
			m_vkPlan = nullptr;
	}
	if(!m_vkPlan)
	{
		m_vkPlan = make_unique<VulkanFFTPlan>(
			npoints, npoints, VulkanFFTPlan::DIRECTION_FORWARD, 1, VulkanFFTPlan::TYPE_COMPLEX);
	}

	//Complex data is stored as interleaved I/Q
	m_rdinbuf.resize(npoints * 2);
	m_rdoutbuf.resize(npoints * 2);
}

void ComplexFFTFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("ComplexFFTFilter::Refresh");
	#endif

	//Make sure we've got valid inputs
	ClearMessages();
	auto din_i = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	auto din_q = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(1));
	auto din_freq = GetInput(2);
	if(!din_i || !din_q || !din_freq)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No I signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at I input");
		else if(!din_i)
			AddErrorMessage("Invalid inputs", "Expect a uniform analog I input");

		if(!GetInput(1))
			AddErrorMessage("Missing inputs", "No Q signal input connected");
		else if(!GetInputWaveform(1))
			AddErrorMessage("Missing inputs", "No waveform available at Q input");
		else if(!din_q)
			AddErrorMessage("Invalid inputs", "Expect a uniform analog Q input");

		if(!din_freq)
			AddErrorMessage("Missing inputs", "No center frequency control input connected");

		SetData(nullptr, 0);
		return;
	}

	//I and Q have to line up sample for sample
	if(din_i->m_timescale != din_q->m_timescale)
	{
		AddErrorMessage("Invalid inputs", "I and Q inputs have different sample rates");
		SetData(nullptr, 0);
		return;
	}

	//An empty input (e.g. a session loaded offline with no waveform data) has nothing to transform
	const size_t npoints = min(din_i->size(), din_q->size());
	if(npoints == 0)
	{
		AddErrorMessage("Missing inputs", "Input waveform is empty");
		SetData(nullptr, 0);
		return;
	}
	LogTrace("ComplexFFTFilter: processing %zu input samples\n", npoints);

	m_xAxisUnit = Unit(Unit::UNIT_MICROHZ);
	m_cachedNumOuts = npoints;
	if( (m_cachedNumPoints != npoints) || !m_vkPlan)
		ReallocateComplexBuffers(npoints);

	//Both positive and negative frequencies are in the output, so the bins cover the whole sample rate
	double fs_per_sample = din_i->m_timescale;
	double sample_ghz = 1e6 / fs_per_sample;
	int64_t bin_uhz = round(sample_ghz * 1e15 / npoints);
	LogTrace("bin_uhz: %" PRIi64 " (%s)\n", bin_uhz, Unit(Unit::UNIT_MICROHZ).PrettyPrint(bin_uhz).c_str());

	//If we have too big a FFT at low sample rate we can run below 1 uHz resolution and bork
	if(bin_uhz == 0)
	{
		SetData(nullptr, 0);
		return;
	}

	//Set up output. The first bin is the lowest frequency, which is center minus half the FFT range.
	int64_t centerUhz = static_cast<int64_t>(din_freq.GetScalarValue()) * 1000000;
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din_i, 0);
	cap->m_timescale = bin_uhz;
	cap->m_triggerPhase = centerUhz - static_cast<int64_t>(npoints / 2) * bin_uhz;
	cap->Resize(npoints);

	//Output scale is the same as the complex spectrogram: amplitude scaled by 2/N, then the coherent power gain of
	//the window function
	float scale = 2.0 / npoints;
	auto window = static_cast<WindowFunction>(m_parameters[m_windowName].GetIntVal());
	switch(window)
	{
		case WINDOW_HAMMING:
			scale *= 1.862;
			break;

		case WINDOW_HANN:
			scale *= 2.013;
			break;

		case WINDOW_BLACKMAN_HARRIS:
			scale *= 2.805;
			break;

		//unit
		case WINDOW_RECTANGULAR:
		default:
			break;
	}

	//Configure the window
	WindowFunctionArgs args;
	args.numActualSamples = npoints;
	args.npoints = npoints;
	args.scale = 2 * M_PI / npoints;
	args.offsetIn = 0;
	args.offsetOut = 0;
	switch(window)
	{
		case WINDOW_HANN:
			args.alpha0 = 0.5;
			break;

		case WINDOW_HAMMING:
			args.alpha0 = 25.0f / 46;
			break;

		default:
			args.alpha0 = 0;
			break;
	}
	args.alpha1 = 1 - args.alpha0;

	{
		NamedDebugRange debugRange(cmdBuf, "ComplexFFTFilter");
		const uint32_t compute_block_count = GetComputeBlockCount(npoints, 64);

		//Apply the window function, interleaving I and Q into the FFT input
		{
			NamedDebugRange shaderRange(cmdBuf, "Window function");

			ComputePipeline* wpipe = nullptr;
			switch(window)
			{
				case WINDOW_BLACKMAN_HARRIS:
					wpipe = &m_blackmanHarrisComputePipeline;
					break;

				case WINDOW_HANN:
				case WINDOW_HAMMING:
					wpipe = &m_cosineSumComputePipeline;
					break;

				default:
				case WINDOW_RECTANGULAR:
					wpipe = &m_rectangularComputePipeline;
					break;
			}

			//Q is bound at 2 rather than 1 to keep commonality with the real valued window functions
			wpipe->BindBufferNonblocking(0, din_i->m_samples, cmdBuf);
			wpipe->BindBufferNonblocking(1, m_rdinbuf, cmdBuf, true);
			wpipe->BindBufferNonblocking(2, din_q->m_samples, cmdBuf);
			wpipe->Dispatch(cmdBuf, args,
				min(compute_block_count, 32768u),
				compute_block_count / 32768 + 1);
			wpipe->AddComputeMemoryBarrier(cmdBuf);
			m_rdinbuf.MarkModifiedFromGpu();
		}

		//Do the actual FFT operation
		{
			NamedDebugRange shaderRange(cmdBuf, "FFT");
			m_vkPlan->AppendForward(m_rdinbuf, m_rdoutbuf, cmdBuf);
		}

		//Convert complex to log magnitude, rotating so the lowest frequency comes first
		{
			NamedDebugRange shaderRange(cmdBuf, "Postprocess");

			const float impedance = 50;
			ComplexToMagnitudeArgs cargs;
			cargs.npoints = npoints;
			cargs.scale = scale * scale / impedance;

			m_complexToLogMagnitudeComputePipeline.BindBufferNonblocking(0, m_rdoutbuf, cmdBuf);
			m_complexToLogMagnitudeComputePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
			m_complexToLogMagnitudeComputePipeline.AddComputeMemoryBarrier(cmdBuf);
			m_complexToLogMagnitudeComputePipeline.Dispatch(cmdBuf, cargs,
				min(compute_block_count, 32768u),
				compute_block_count / 32768 + 1);
		}
	}

	cap->MarkModifiedFromGpu();

	//If doing peak detection, block now
	if(IsPeakSearchNeeded())
	{
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		//Peak search (for now this runs on the CPU)
		FindPeaks(cap, cmdBuf, queue);
	}
}
