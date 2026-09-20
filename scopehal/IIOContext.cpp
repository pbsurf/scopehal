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
	@brief Implementation of IIOContext
	@ingroup sdrdrivers
 */

#ifdef HAS_IIO

#include "scopehal.h"
#include "IIOLibContext.h"
#include "IIOMockContext.h"

using namespace std;

unique_ptr<IIOContext> IIOContext::Open(const string& uri)
{
	if(uri.find("mock:") == 0)
		return IIOMockContext::Create(uri);

	//Be forgiving if the user typed a bare hostname or IP address
	if(uri.find(':') == string::npos)
		return IIOLibContext::Create("ip:" + uri);

	return IIOLibContext::Create(uri);
}

vector<pair<string, string> > IIOContext::Scan()
{
	return IIOLibContext::Scan();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Typed attribute helpers

bool IIOContext::ReadChannelAttrInt(
	const string& dev, const string& chan, bool output, const string& attr, int64_t& value)
{
	string tmp;
	if(!ReadChannelAttr(dev, chan, output, attr, tmp))
		return false;

	char* end = nullptr;
	errno = 0;
	value = strtoll(tmp.c_str(), &end, 10);
	if( (end == tmp.c_str()) || (errno != 0) )
	{
		LogError("IIO attribute %s/%s/%s value \"%s\" is not an integer\n",
			dev.c_str(), chan.c_str(), attr.c_str(), tmp.c_str());
		return false;
	}
	return true;
}

bool IIOContext::WriteChannelAttrInt(
	const string& dev, const string& chan, bool output, const string& attr, int64_t value)
{
	return WriteChannelAttr(dev, chan, output, attr, to_string(value));
}

bool IIOContext::ReadChannelAttrDouble(
	const string& dev, const string& chan, bool output, const string& attr, double& value)
{
	string tmp;
	if(!ReadChannelAttr(dev, chan, output, attr, tmp))
		return false;

	//Some attributes (hardwaregain, rssi) have trailing units like "71.000000 dB", strtod stops at the space
	char* end = nullptr;
	errno = 0;
	value = strtod(tmp.c_str(), &end);
	if( (end == tmp.c_str()) || (errno != 0) )
	{
		LogError("IIO attribute %s/%s/%s value \"%s\" is not a number\n",
			dev.c_str(), chan.c_str(), attr.c_str(), tmp.c_str());
		return false;
	}
	return true;
}

bool IIOContext::WriteChannelAttrDouble(
	const string& dev, const string& chan, bool output, const string& attr, double value)
{
	char tmp[64];
	snprintf(tmp, sizeof(tmp), "%.6f", value);
	return WriteChannelAttr(dev, chan, output, attr, tmp);
}

#endif
