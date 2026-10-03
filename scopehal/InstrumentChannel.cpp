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
	@author Andrew D. Zonenberg
	@brief Implementation of InstrumentChannel
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

InstrumentChannel::InstrumentChannel(
	Instrument* inst,
	const string& hwname,
	const string& color,
	Unit xunit,
	size_t index)
	: m_displaycolor(color)
	, m_visibilityMode(VIS_AUTO)
	, m_instrument(inst)
	, m_hwname(hwname)
	, m_displayname(hwname)
	, m_index(index)
	, m_xAxisUnit(xunit)
{
}

InstrumentChannel::InstrumentChannel(
	Instrument* inst,
	const string& hwname,
	const string& color,
	Unit xunit,
	Unit yunit,
	Stream::StreamType stype,
	size_t index)
	: m_displaycolor(color)
	, m_visibilityMode(VIS_AUTO)
	, m_instrument(inst)
	, m_hwname(hwname)
	, m_displayname(hwname)
	, m_index(index)
	, m_xAxisUnit(xunit)
{
	AddStream(yunit, "data", stype);
}

InstrumentChannel::~InstrumentChannel()
{
	ClearStreams();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

InstrumentChannel::PhysicalConnector InstrumentChannel::GetPhysicalConnector()
{
	return CONNECTOR_BNC;
}

/**
	@brief Sets the human-readable nickname for this channel, as displayed in the GUI
 */
void InstrumentChannel::SetDisplayName(string name)
{
	m_displayname = name;
}

/**
	@brief Gets the human-readable nickname for this channel, as displayed in the GUI
 */
string InstrumentChannel::GetDisplayName()
{
	return m_displayname;
}

/**
	@brief Gets the display color of a stream: its own color if it has one, otherwise the channel's m_displaycolor
 */
string InstrumentChannel::GetStreamDisplayColor(size_t stream) const
{
	auto it = m_streamDisplayColors.find(stream);
	if(it != m_streamDisplayColors.end())
		return it->second;
	return m_displaycolor;
}

/**
	@brief Sets the display color of a stream (HTML hex notation)

	@param stream	Stream index
	@param color	Color for the stream, or an empty string to use the channel's m_displaycolor again
 */
void InstrumentChannel::SetStreamDisplayColor(size_t stream, const string& color)
{
	if(color.empty())
		m_streamDisplayColors.erase(stream);
	else
		m_streamDisplayColors[stream] = color;
}

/**
	@brief Saves the per-stream display colors under node["streamcolors"]

	Channels with more than one stream always get the key, even if it's empty, so that loading them doesn't bring
	back a default stream color set by the driver after the user went back to the channel color.
 */
void InstrumentChannel::SerializeStreamDisplayColors(YAML::Node& node) const
{
	if( (m_streams.size() < 2) && m_streamDisplayColors.empty())
		return;

	YAML::Node colors(YAML::NodeType::Map);
	for(auto& it : m_streamDisplayColors)
		colors["stream" + to_string(it.first)] = it.second;
	node["streamcolors"] = colors;
}

/**
	@brief Loads per-stream display colors saved by SerializeStreamDisplayColors()

	If there is no node["streamcolors"] (e.g. files saved before stream colors existed), any default stream colors
	set by the driver are kept.
 */
void InstrumentChannel::LoadStreamDisplayColors(const YAML::Node& node)
{
	auto colors = node["streamcolors"];
	if(!colors)
		return;

	m_streamDisplayColors.clear();
	for(auto it : colors)
	{
		auto key = it.first.as<string>();
		if(key.rfind("stream", 0) != 0)
			continue;
		auto index = key.substr(6);
		if(index.empty() || (index.find_first_not_of("0123456789") != string::npos) )
			continue;
		m_streamDisplayColors[stoul(index)] = it.second.as<string>();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stream management

/**
	@brief Clears out any existing streams
 */
void InstrumentChannel::ClearStreams()
{
	for(auto& s : m_streams)
		delete s.m_waveform;
	m_streams.clear();
}

/**
	@brief Adds a new data stream to the channel

	@return Index of the new stream
 */
size_t InstrumentChannel::AddStream(Unit yunit, const string& name, Stream::StreamType stype, uint8_t flags)
{
	size_t index = m_streams.size();
	m_streams.push_back(Stream(yunit, name, stype, flags));

	//Allocate space for sinks list
	if(m_sinks.size() <= index)
		m_sinks.resize(index + 1);

	return index;
}

/**
	@brief Sets the waveform data for a given stream, replacing any previous waveform.

	Calling this function with pNew == GetData() is a legal no-op.

	Any existing waveform is deleted, unless it is the same as pNew.
 */
void InstrumentChannel::SetData(WaveformBase* pNew, size_t stream)
{
	if(m_streams[stream].m_waveform == pNew)
		return;

	if(m_streams[stream].m_waveform != NULL)
		delete m_streams[stream].m_waveform;
	m_streams[stream].m_waveform = pNew;
}

bool InstrumentChannel::ShouldPersistWaveform()
{
	//Default to persisting everything
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Download progress

InstrumentChannel::DownloadState InstrumentChannel::GetDownloadState()
{
	return DownloadState::DOWNLOAD_UNKNOWN;
}

float InstrumentChannel::GetDownloadProgress()
{
	return 0.0;
}

double InstrumentChannel::GetDownloadStartTime()
{
	return 0.0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Filter graph stubs

void InstrumentChannel::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{

}
