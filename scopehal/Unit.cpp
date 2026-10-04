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
	@brief Implementation of Unit
	@ingroup datamodel
 */

#include "scopehal.h"

#include <cinttypes>
#include <numeric>

using namespace std;

#ifdef _WIN32
string Unit::m_slocale;
#else
locale_t Unit::m_locale;
locale_t Unit::m_defaultLocale;
#endif
char Unit::m_decimalSeparator = '.';

/**
	@brief Constructs a new unit from a string
 */
Unit::Unit(const string& rhs)
{
	if(rhs == "fs")
		m_type = UNIT_FS;
	else if(rhs == "pm")
		m_type = UNIT_PM;
	else if(rhs == "Hz")
		m_type = UNIT_HZ;
	else if(rhs == "V")
		m_type = UNIT_VOLTS;
	else if(rhs == "A")
		m_type = UNIT_AMPS;
	else if(rhs == "Ω")
		m_type = UNIT_OHMS;
	else if(rhs == "b/s")
		m_type = UNIT_BITRATE;
	else if(rhs == "%")
		m_type = UNIT_PERCENT;
	else if(rhs == "dB")
		m_type = UNIT_DB;
	else if(rhs == "dBm")
		m_type = UNIT_DBM;
	else if(rhs == "unitless (linear)")
		m_type = UNIT_COUNTS;
	else if(rhs == "unitless (log)")
		m_type = UNIT_COUNTS_SCI;
	else if(rhs == "log BER")
		m_type = UNIT_LOG_BER;
	else if(rhs == "ratio (scientific)")
		m_type = UNIT_RATIO_SCI;
	else if(rhs == "sa/s")
		m_type = UNIT_SAMPLERATE;
	else if(rhs == "sa")
		m_type = UNIT_SAMPLEDEPTH;
	else if(rhs == "W")
		m_type = UNIT_WATTS;
	else if(rhs == "UI")
		m_type = UNIT_UI;
	else if(rhs == "°")
		m_type = UNIT_DEGREES;
	else if(rhs == "RPM")
		m_type = UNIT_RPM;
	else if(rhs == "°C")
		m_type = UNIT_CELSIUS;
	else if(rhs == "ρ")
		m_type = UNIT_RHO;
	else if(rhs == "mV")
		m_type = UNIT_MILLIVOLTS;
	else if(rhs == "μV")
		m_type = UNIT_MICROVOLTS;
	else if(rhs == "Vs")
		m_type = UNIT_VOLT_SEC;
	else if(rhs == "hex")
		m_type = UNIT_HEXNUM;
	else if(rhs == "B")
		m_type = UNIT_BYTES;
	else if(rhs == "W/m²/nm")
		m_type = UNIT_W_M2_NM;
	else if(rhs == "W/m²")
		m_type = UNIT_W_M2;
	else if(rhs == "μA")
		m_type = UNIT_MICROAMPS;
	else if(rhs == "μHz")
		m_type = UNIT_MICROHZ;
	else if(rhs == "F")
		m_type = UNIT_FARADS;
	else
		LogWarning("Unrecognized unit \"%s\"\n", rhs.c_str());
}

bool Unit::IsLogarithmic()
{
	switch(m_type)
	{
		case UNIT_DB:
		case UNIT_DBM:
		case UNIT_LOG_BER:
		case UNIT_COUNTS_SCI:
			return true;

		default:
			return false;
	}
}

/**
	@brief Converts this unit to a string in long format
 */
string Unit::ToStringLong() const
{
	switch(m_type)
	{
		case UNIT_DEGREES:
			return "angular degrees";

		default:
			return ToString();
	}
}

/**
	@brief Converts this unit to a string
 */
string Unit::ToString() const
{
	switch(m_type)
	{
		case UNIT_FS:
			return "fs";

		case UNIT_PM:
			return "pm";

		case UNIT_HZ:
			return "Hz";

		case UNIT_VOLTS:
			return "V";

		case UNIT_AMPS:
			return "A";

		case UNIT_OHMS:
			return "Ω";

		case UNIT_BITRATE:
			return "b/s";

		case UNIT_PERCENT:
			return "%";

		case UNIT_DB:
			return "dB";

		case UNIT_DBM:
			return "dBm";

		case UNIT_COUNTS:
			return "unitless (linear)";

		case UNIT_COUNTS_SCI:
			return "unitless (log)";

		case UNIT_RATIO_SCI:
			return "ratio (scientific)";

		case UNIT_LOG_BER:
			return "log BER";

		case UNIT_SAMPLERATE:
			return "sa/s";

		case UNIT_SAMPLEDEPTH:
			return "sa";

		case UNIT_WATTS:
			return "W";

		case UNIT_UI:
			return "UI";

		case UNIT_DEGREES:
			return "°";

		case UNIT_RPM:
			return "RPM";

		case UNIT_CELSIUS:
			return "°C";

		case UNIT_RHO:
			return "ρ";

		case UNIT_MILLIVOLTS:
			return "mV";

		case UNIT_MICROVOLTS:
			return "μV";

		case UNIT_MICROAMPS:
			return "μA";

		case UNIT_MICROHZ:
			return "μHz";

		case UNIT_VOLT_SEC:
			return "Vs";

		case UNIT_HEXNUM:
			return "hex";

		case UNIT_BYTES:
			return "B";

		case UNIT_W_M2_NM:
			return "W/m²/nm";

		case UNIT_W_M2:
			return "W/m²";

		case UNIT_FARADS:
			return "F";

		default:
			return "unknown";
	}
}

///@brief SI prefixes, largest first
static const struct
{
	///@brief Power of ten of the prefix
	int exponent;

	///@brief The prefix
	const char* prefix;
} g_siPrefixes[] =
{
	{ 12, "T" },
	{ 9, "G" },
	{ 6, "M" },
	{ 3, "k" },
	{ 0, "" },
	{ -3, "m" },
	{ -6, "μ" },
	{ -9, "n" },
	{ -12, "p" },
	{ -15, "f" }
};

/**
	@brief Chooses the SI prefix for a value: the largest one that keeps the number shown at least 1

	Zero has no prefix (or the closest one to none that the unit uses), so it's "0 V" rather than "0 fV".

	@param num		Magnitude of the value, in the unit it is stored in
	@param base		Power of ten of the unit values are stored in, relative to the unit shown (-15 for femtoseconds)
	@param lowest	Power of ten of the smallest prefix to use. This is used if no prefix keeps the number at least 1.
	@param highest	Power of ten of the largest prefix to use
	@param factor	Set to the factor to multiply values by to get the number shown
	@param prefix	Set to the prefix
 */
static void ChooseSIPrefix(double num, int base, int lowest, int highest, double& factor, string& prefix)
{
	int exponent = lowest;
	prefix = "";
	for(auto& p : g_siPrefixes)
	{
		if( (p.exponent > highest) || (p.exponent < lowest) )
			continue;

		exponent = p.exponent;
		prefix = p.prefix;
		if(num == 0)
		{
			if(p.exponent <= 0)
				break;
		}
		else if(num >= pow(10, p.exponent - base))
			break;
	}

	factor = pow(10, base - exponent);
}

/**
	@brief Gets how a value is shown: the scale factor, prefix, and text around the number

	@param reference	The value to choose the prefix for
 */
Unit::Scaling Unit::GetScaling(double reference) const
{
	Scaling s;
	s.factor = 1;
	s.space = true;
	double num = fabs(reference);

	//Most units use all of the SI prefixes
	bool si = false;

	switch(m_type)
	{
		//Units that aren't SI base units. Values are stored in a smaller unit, and only some prefixes are used.
		case UNIT_FS:
			s.suffix = "s";
			ChooseSIPrefix(num, -15, -15, 0, s.factor, s.prefix);
			break;

		case UNIT_PM:
			s.suffix = "m";
			ChooseSIPrefix(num, -12, -12, 3, s.factor, s.prefix);
			break;

		case UNIT_MICROAMPS:
			s.suffix = "A";
			ChooseSIPrefix(num, -6, -6, 6, s.factor, s.prefix);
			break;

		case UNIT_MICROHZ:
			s.suffix = "Hz";
			ChooseSIPrefix(num, -6, -6, 9, s.factor, s.prefix);
			break;

		case UNIT_MICROVOLTS:
			s.suffix = "V";
			ChooseSIPrefix(num, -6, -6, 6, s.factor, s.prefix);
			break;

		//Bytes: use binary rather than decimal scaling factors
		case UNIT_BYTES:
			s.suffix = "B";
			if(num >= 1024*1024*1024)
			{
				s.factor = 1.0 / (1024*1024*1024);
				s.prefix = "G";
			}
			else if(num >= 1024*1024)
			{
				s.factor = 1.0 / (1024*1024);
				s.prefix = "M";
			}
			else if(num >= 1024)
			{
				s.factor = 1.0 / 1024;
				s.prefix = "k";
			}
			break;

		//No prefixes
		case UNIT_MILLIVOLTS:
			s.suffix = "mV";
			break;

		case UNIT_DEGREES:
			s.suffix = "°";
			break;

		case UNIT_CELSIUS:
			s.suffix = "°C";
			break;

		case UNIT_DBM:
			s.suffix = "dBm";
			break;

		case UNIT_DB:
			s.suffix = "dB";
			break;

		//Convert fractional num to percentage
		case UNIT_PERCENT:
			s.suffix = "%";
			s.factor = 100;
			break;

		case UNIT_HEXNUM:
			s.numprefix = "0x";
			s.space = false;
			break;

		case UNIT_COUNTS:
			s.space = false;
			break;

		case UNIT_LOG_BER:
			break;

		//SI prefixes
		case UNIT_UI:
			s.suffix = " UI";	//move the space next to the number
			s.space = false;
			si = true;
			break;

		case UNIT_HZ:			s.suffix = "Hz";	si = true;	break;
		case UNIT_SAMPLERATE:	s.suffix = "S/s";	si = true;	break;
		case UNIT_SAMPLEDEPTH:	s.suffix = "S";		si = true;	break;
		case UNIT_VOLTS:		s.suffix = "V";		si = true;	break;
		case UNIT_AMPS:			s.suffix = "A";		si = true;	break;
		case UNIT_OHMS:			s.suffix = "Ω";		si = true;	break;
		case UNIT_WATTS:		s.suffix = "W";		si = true;	break;
		case UNIT_RHO:			s.suffix = "ρ";		si = true;	break;
		case UNIT_BITRATE:		s.suffix = "bps";	si = true;	break;
		case UNIT_RPM:			s.suffix = "RPM";	si = true;	break;
		case UNIT_FARADS:		s.suffix = "F";		si = true;	break;
		case UNIT_COUNTS_SCI:	s.suffix = "#";		si = true;	break;
		case UNIT_VOLT_SEC:		s.suffix = "Vs";	si = true;	break;
		default:										si = true;	break;
	}

	if(si)
		ChooseSIPrefix(num, 0, -15, 12, s.factor, s.prefix);

	return s;
}

/**
	@brief Prints a value with SI scaling factors

	@param value				The value
	@param sigfigs				Number of significant digits to display. If not positive, as many as needed are shown
								(up to 9 after the decimal point).
	@param useDisplayLocale		True if the string is formatted for display (user's locale)
								False if the string is formatted for serialization ("C" locale regardless of user pref)
 */
string Unit::PrettyPrint(double value, int sigfigs, bool useDisplayLocale) const
{
	return PrettyPrintScaled(value, useDisplayLocale, sigfigs);
}

/**
	@brief Prints a value with SI scaling factors in a format suitable for tabular display

	@param value				The value
	@param leftdigits			Minimum width of the number, including the decimal point and sign
	@param rightdigits			Number of digits after the decimal point
 */
string Unit::PrettyPrintTabular(double value, int leftdigits, int rightdigits) const
{
	return PrettyPrintScaled(value, true, -1, leftdigits, rightdigits);
}

/**
	@brief Prints a value with SI scaling factors, with digits chosen by PrettyPrint() or PrettyPrintTabular()

	@param value				The value
	@param useDisplayLocale		True if the string is formatted for display (user's locale)
								False if the string is formatted for serialization ("C" locale regardless of user pref)
	@param sigfigs				Number of significant digits to display, if leftdigits is negative. If not positive
								either, as many digits as needed are shown (up to 9 after the decimal point).
	@param leftdigits			If not negative, minimum width of the number, as for printf()
	@param rightdigits			Number of digits after the decimal point, if leftdigits is not negative
 */
string Unit::PrettyPrintScaled(double value, bool useDisplayLocale, int sigfigs, int leftdigits, int rightdigits) const
{
	// Special handling for overload value
	if(value >= std::numeric_limits<double>::max())
		return UNIT_OVERLOAD_LABEL;

	if(useDisplayLocale)
		SetPrintingLocale();

	auto s = GetScaling(value);
	double value_rescaled = value * s.factor;

	char tmp[128];
	switch(m_type)
	{
		case UNIT_LOG_BER:		//special formatting for BER since it's already logarithmic
			snprintf(tmp, sizeof(tmp), "%.2e", pow(10, value));
			break;

		case UNIT_RATIO_SCI:
			snprintf(tmp, sizeof(tmp), "%.2e", value);
			break;

		//NOTE: only works for 32 bit values or smaller
		case UNIT_HEXNUM:
			snprintf(tmp, sizeof(tmp), "%x", static_cast<uint32_t>(value));
			break;

		default:
			{
				const char* space = s.space ? " " : "";

				if( (leftdigits < 0) && (sigfigs > 0) )
				{
					leftdigits = 0;
					if(fabs(value_rescaled) > 1000)			//shouldn't have more than 4 digits w/ SI scaling
						leftdigits = 4;
					else if(fabs(value_rescaled) > 100)
						leftdigits = 3;
					else if(fabs(value_rescaled) > 10)
						leftdigits = 2;
					else if(fabs(value_rescaled) > 1)
						leftdigits = 1;
					rightdigits = sigfigs - leftdigits;
				}

				if(leftdigits >= 0)
				{
					string format = string("%") + to_string(leftdigits) + "." + to_string(rightdigits) + "f%s%s%s";
					snprintf(tmp, sizeof(tmp), format.c_str(), value_rescaled, space, s.prefix.c_str(),
						s.suffix.c_str());
				}

				//If not a round number, add as many digits as needed to show it (up to 9)
				//The check for "close enough to a round number" is relative to the size of the value, to ignore
				//floating point noise (e.g. 0.1f is really 0.100000001490116) but not real differences between
				//large values (e.g. 1.0004 GHz is not 1 GHz).
				//For exact integer values that need more digits than this can show, use PrettyPrintInt64().
				else
				{
					const int maxDigits = 9;
					int digits = 0;
					for(; digits < maxDigits; digits++)
					{
						double scaled = fabs(value_rescaled) * pow(10, digits);
						if(fabs(round(scaled) - scaled) <= 1e-6 * scaled)
							break;
					}
					snprintf(tmp, sizeof(tmp), "%.*f%s%s%s", digits, value_rescaled, space, s.prefix.c_str(),
						s.suffix.c_str());
				}
			}
			break;
	}

	SetDefaultLocale();
	return s.numprefix + string(tmp);
}

/**
	@brief Prints a value with SI scaling factors

	@param value				The value
	@param digits				Number of significant digits to display
	@param useDisplayLocale		True if the string is formatted for display (user's locale)
								False if the string is formatted for serialization ("C" locale regardless of user pref)
 */
string Unit::PrettyPrintInt64(int64_t value, int sigfigs, bool useDisplayLocale) const
{
	return PrettyPrintInt64WithScale(value, value, sigfigs, useDisplayLocale);
}

/**
	@brief Prints a value with the SI scaling factor that would be used for a different value

	This is for printing several values with the same prefix, such as the labels on an axis, so that for example zero
	is shown as "0 μs" rather than "0 fs" next to "1 μs".

	@param value				The value
	@param scaleReference		Value used to choose the scaling factor (typically the largest magnitude of the set
								of values being printed; a smaller magnitude than value may overflow)
	@param sigfigs				Number of significant digits to display
	@param useDisplayLocale		True if the string is formatted for display (user's locale)
								False if the string is formatted for serialization ("C" locale regardless of user pref)
 */
string Unit::PrettyPrintInt64WithScale(int64_t value, int64_t scaleReference, int sigfigs, bool useDisplayLocale) const
{
	if(useDisplayLocale)
		SetPrintingLocale();

	auto s = GetScaling(scaleReference);

	//Apply the rescaling in the integer domain
	int64_t mulFactor = s.factor;
	int64_t divFactor = round(1.0 / s.factor);

	int64_t value_rescaled;
	if(s.factor > 1)
		value_rescaled = value * mulFactor;
	else
		value_rescaled = value / divFactor;

	char tmp[128];
	switch(m_type)
	{
		case UNIT_LOG_BER:		//special formatting for BER since it's already logarithmic
			snprintf(tmp, sizeof(tmp), "%.2e", pow(10, value_rescaled));
			break;

		case UNIT_RATIO_SCI:
			snprintf(tmp, sizeof(tmp), "%.2e", (float)value_rescaled);
			break;

		case UNIT_HEXNUM:
			snprintf(tmp, sizeof(tmp), "%" PRIx64, value_rescaled);
			break;

		default:
			{
				//Default if not specified is 4 digits after the decimal point
				if(sigfigs < 0)
					sigfigs = 4;

				//Cap max digits so that 10^digits still fits in an int64
				if(sigfigs > MAX_INT64_DECIMALS)
					sigfigs = MAX_INT64_DECIMALS;

				//Split the magnitude into the whole part and the digits after the decimal point, exactly.
				//The digits are truncated, not rounded, like everything else here.
				bool negative = (value < 0);
				uint64_t magnitude = negative ? (0 - static_cast<uint64_t>(value)) : static_cast<uint64_t>(value);
				uint64_t whole;
				uint64_t fraction = 0;
				if(s.factor > 1)
					whole = magnitude * mulFactor;
				else
				{
					whole = magnitude / divFactor;
					uint64_t remainder = magnitude % divFactor;

					//Long division, one digit at a time. This can't overflow since the remainder is always less than
					//divFactor (at most 1e15), unlike multiplying by 10^digits up front.
					for(int i=0; i<sigfigs; i++)
					{
						remainder *= 10;
						fraction = fraction*10 + (remainder / divFactor);
						remainder %= divFactor;
					}
				}

				//Don't show a minus sign in front of zero
				const char* sign = (negative && ( (whole != 0) || (fraction != 0) ) ) ? "-" : "";

				if(sigfigs == 0)
					snprintf(tmp, sizeof(tmp), "%s%" PRIu64, sign, whole);

				else
				{
					//Use correct decimal separator for user's locale if needed
					char separator = useDisplayLocale ? m_decimalSeparator : '.';
					snprintf(tmp, sizeof(tmp), "%s%" PRIu64 "%c%0*" PRIu64, sign, whole, separator, sigfigs, fraction);

					//Trim zeroes at right
					ssize_t n = strlen(tmp) - 1;
					for(; n > 0; n--)
					{
						if(tmp[n] == '0')
							tmp[n] = '\0';
						else
							break;
					}

					//Trim trailing decimal point
					if(tmp[n] == separator)
						tmp[n] = '\0';
				}
			}
			break;
	}

	SetDefaultLocale();
	return s.numprefix + string(tmp) + (s.space ? " " : "") + s.prefix + s.suffix;
}

/**
	@brief Prints an integer value with SI scaling factors, showing only the digits that are meaningful

	This is for values that can't be known any more precisely than some resolution, such as a position picked with
	the mouse on a plot, where adjacent pixels are a fixed distance apart. Digits below the resolution are not shown,
	and the value is rounded to the last digit shown rather than truncated.

	@param value				The value
	@param resolution			Smallest difference that matters, in the same units as value (must be positive,
								otherwise the value is shown as precisely as possible)
	@param useDisplayLocale		True if the string is formatted for display (user's locale)
								False if the string is formatted for serialization ("C" locale regardless of user pref)
 */
string Unit::PrettyPrintInt64WithResolution(int64_t value, double resolution, bool useDisplayLocale) const
{
	int digits = MAX_INT64_DECIMALS;

	if( (resolution > 0) && isfinite(resolution) )
	{
		//Figure out how many digits after the decimal point are needed for the last one to be no larger than the
		//resolution, so that neighboring positions are shown as different values
		//(this must use the same scaling as PrettyPrintInt64(), including special cases for units like uHz that
		//aren't SI base units, or the digits will be off by the difference)
		double scaleFactor = GetScaling(value).factor;
		double scaledResolution = resolution * scaleFactor;
		digits = ceil(-log10(scaledResolution) - 1e-9);
		digits = max(0, min(MAX_INT64_DECIMALS, digits));

		//Round to the last digit we show, if that's a coarser step than the native resolution of the value
		double place = pow(10, -digits) / scaleFactor;
		if(place > 1.5)
		{
			int64_t step = llround(place);
			int64_t half = step / 2;
			value = ( (value >= 0) ? (value + half) : (value - half) ) / step * step;
		}
	}

	return PrettyPrintInt64(value, digits, useDisplayLocale);
}

/**
	@brief Prints a value with SI scaling factors and unnecessarily significant sub-pixel digits removed

	The rangeMin/rangeMax values are typically used to ensure all axis labels on a graph use consistent units,
	for example 0.5V / 1.0V / 1.5V rather than 500 mV / 1.0V / 1.5V.

	The pixelMin and pixelMax values are used to determine how many digits are actually significant. Rounding is in the
	upward direction.

	For example, if the pixel covers values 1.3979 to 1.4152, this function will return "1.4".

	@param pixelMin		Value at the lowest end of the pixel being labeled
	@param pixelMax		Value at the highest end of the pixel being labeled
	@param rangeMin		Value at the lowest end of the channel range
	@param rangeMax		Value at the highest end of the channel range
 */
string Unit::PrettyPrintRange(double pixelMin, double pixelMax, double rangeMin, double rangeMax) const
{
	SetPrintingLocale();

	//Figure out the scale factor to use. Use the full-scale range to select the factor even if we're small here
	auto s = GetScaling(max(fabs(rangeMin), fabs(rangeMax)));

	//Swap values if they're reversed
	if(fabs(pixelMin) > fabs(pixelMax))
		swap(pixelMin, pixelMax);

	//Get the actual values to print
	double valueMinRescaled = pixelMin * s.factor;
	double valueMaxRescaled = pixelMax * s.factor;

	//Special case for log BER which is already logarithmic and doesn't need scaling
	const size_t buflen = 32;
	char tmp1[buflen];
	char tmp2[buflen];
	if(m_type == Unit::UNIT_LOG_BER)
	{
		snprintf(tmp1, sizeof(tmp1), "1e%.0f", valueMinRescaled);

		SetDefaultLocale();
		return string(tmp1);
	}

	//Do the actual float to ascii conversion
	if(m_type == Unit::UNIT_HEXNUM)
	{
		snprintf(tmp1, sizeof(tmp1), "%" PRIx64, (int64_t)valueMinRescaled);
		snprintf(tmp2, sizeof(tmp2), "%" PRIx64, (int64_t)valueMaxRescaled);
	}
	else
	{
		snprintf(tmp1, sizeof(tmp1), "%.5f", valueMinRescaled);
		snprintf(tmp2, sizeof(tmp2), "%.5f", valueMaxRescaled);
	}

	//Special case: if zero is somewhere in the pixel, just print zero
	string out;
	if( (valueMinRescaled <= 0) && (valueMaxRescaled >= 0) )
		out = "0";

	else
	{
		size_t i = 0;

		//Minus sign just gets echoed as-is
		//(we know both sides are negative if we get here, no need to check max value)
		if(valueMinRescaled < 0)
		{
			out += "-";
			i = 1;
		}

		//Pick out only the significant digits (tmp2 is always the larger magnitude)
		bool foundDecimal = false;
		for(; i<buflen; i++)
		{
			//If either string ends, stop
			if( (tmp1[i] == '\0') || (tmp2[i] == '\0') )
				break;

			//If both digits are the same, echo to the output
			else if(tmp1[i] == tmp2[i])
			{
				out += tmp1[i];
				if(!isxdigit(tmp1[i]))
					foundDecimal = true;
			}

			//Mismatch! Figure out how to handle it
			else
			{
				//Mismatched significant digit after decimal (10.3, 10.4): just print the bigger digit and stop
				if(foundDecimal)
					out += tmp2[i];

				//Mismatched significant digit before decimal (125, 133): print bigger digit then zeroes
				else
				{
					out += tmp2[i];
					i++;

					//Pad with zeroes until we hit a decimal separator or the end of the number
					for(; i<buflen; i++)
					{
						if(!isxdigit(tmp2[i]))
							break;
						out += '0';
					}
				}

				break;
			}
		}
	}

	//Special case: don't display negative zero
	if(out == "-0")
		out = "0";

	SetDefaultLocale();
	return s.numprefix + out + (s.space ? " " : "") + s.prefix + s.suffix;
}

///@brief Where the number is in a string, as found by FindNumber()
struct NumberInText
{
	///@brief Position of the sign, or of the first digit if there's no sign
	size_t signPos;

	///@brief Position of the first digit (or decimal mark, if there are no integer digits)
	size_t digitsStart;

	///@brief Position of the decimal mark, or string::npos if there isn't one
	size_t mark;

	///@brief Position of the first character after the number
	size_t numEnd;

	///@brief True if the number has a "-" sign
	bool negative;

	///@brief True if the number has a "+" sign
	bool explicitPlus;

	///@brief Number of digits before the decimal mark
	int intDigits;

	///@brief Number of digits after the decimal mark
	int fracDigits;
};

/**
	@brief Finds the number at the start of a string: optional spaces and sign, then digits with at most one decimal mark
 */
static NumberInText FindNumber(const string& text)
{
	size_t n = text.size();
	NumberInText num;

	size_t i = 0;
	while( (i < n) && isspace(static_cast<unsigned char>(text[i])) )
		i ++;
	num.signPos = i;
	num.negative = false;
	num.explicitPlus = false;
	if( (i < n) && ( (text[i] == '-') || (text[i] == '+') ) )
	{
		num.negative = (text[i] == '-');
		num.explicitPlus = !num.negative;
		i ++;
	}
	num.digitsStart = i;
	num.mark = string::npos;
	while(i < n)
	{
		char c = text[i];
		if(isdigit(static_cast<unsigned char>(c)))
			i ++;
		else if( ( (c == '.') || (c == ',') ) && (num.mark == string::npos) )
		{
			num.mark = i;
			i ++;
		}
		else
			break;
	}
	num.numEnd = i;

	num.intDigits = static_cast<int>( ((num.mark == string::npos) ? num.numEnd : num.mark) - num.digitsStart );
	num.fracDigits = (num.mark == string::npos) ? 0 : static_cast<int>(num.numEnd - num.mark - 1);
	return num;
}

/**
	@brief Finds the place value (as a power of ten) of the digit that StepNumericText() steps for a cursor position

	@param num		The number, as found by FindNumber()
	@param cursor	Cursor position. Positions outside the number are treated as being at its nearest end.
 */
static int CursorExponent(const NumberInText& num, int cursor)
{
	//Cursor relative to the first digit
	int rel = min(max(cursor, static_cast<int>(num.digitsStart)), static_cast<int>(num.numEnd)) -
		static_cast<int>(num.digitsStart);

	//After the decimal mark. Right after the mark itself is the ones digit, otherwise count fractional digits.
	if( (num.mark != string::npos) && (rel > num.intDigits) )
		return (rel == num.intDigits + 1) ? 0 : -(rel - num.intDigits - 1);

	return num.intDigits - rel;
}

/**
	@brief Increments or decrements the digit to the left of the cursor in a number being edited by the user

	The text is expected to look like the output of PrettyPrint(): an optional sign, digits with an optional decimal
	mark ("." or ","), then optionally an SI prefix and unit (for example "2.4 GHz"). Anything before or after the
	number is preserved as-is.

	This works on the digits of the text itself rather than on the value it represents. The number of decimal places,
	prefix, and unit never change, so "999 MHz" becomes "1000 MHz", not "1 GHz", and the cursor stays put even
	when the user holds down the key. The result can be parsed with ParseString() in the usual way.

	The digit stepped is the one immediately before the cursor, so with the cursor at "2.4| GHz" the step is 0.1, with
	"2|.4 GHz" or "2.|4 GHz" it is 1, and with "|2.4 GHz" it is 10. Carries and borrows propagate, and stepping down
	through zero flips the sign.

	If the cursor is beyond the number (for example "2.4 |GHz"), another decimal place is added and that is stepped:
	"2.4 |GHz" becomes "2.41| GHz" going up or "2.39| GHz" going down, with the cursor just after the new digit so that
	repeated steps keep changing the same place.

	@param text			Text being edited
	@param cursor		Cursor position, as a byte offset into text
	@param increment	True to add one to the digit, false to subtract one
	@param newText		Text after stepping
	@param newCursor	Cursor position after stepping, adjusted for any digits or sign that were added or removed

	@return				True if a number was found and stepped. If false, newText and newCursor are not modified.
 */
bool Unit::StepNumericText(const string& text, int cursor, bool increment, string& newText, int& newCursor)
{
	size_t n = text.size();

	auto num = FindNumber(text);
	size_t signPos = num.signPos;
	bool negative = num.negative;
	bool explicitPlus = num.explicitPlus;
	size_t digitsStart = num.digitsStart;
	size_t mark = num.mark;
	size_t numEnd = num.numEnd;
	int intDigits = num.intDigits;
	int fracDigits = num.fracDigits;
	if(intDigits + fracDigits == 0)
		return false;

	//If the cursor is beyond the number, add another decimal place and step that one. The cursor ends up right after the
	//new digit, so this can only recurse once.
	if(cursor > static_cast<int>(numEnd))
	{
		string widened = text.substr(0, numEnd);

		//Use the same decimal mark if we already have one, otherwise the one for the user's locale
		if(mark == string::npos)
			widened += m_decimalSeparator;
		widened += '0';
		int widenedCursor = static_cast<int>(widened.size());
		widened += text.substr(numEnd);

		return StepNumericText(widened, widenedCursor, increment, newText, newCursor);
	}

	//Work out which digit we're stepping
	int exponent = CursorExponent(num, cursor);

	//Digits of the magnitude, least significant first. Leave room for the digit we're stepping and for a carry.
	vector<int> d;
	for(size_t j=numEnd; j>digitsStart; j--)
	{
		if(isdigit(static_cast<unsigned char>(text[j-1])))
			d.push_back(text[j-1] - '0');
	}
	size_t pos = exponent + fracDigits;
	if(d.size() < pos + 1)
		d.resize(pos + 1, 0);
	d.push_back(0);

	auto isZero = [&]()
	{
		for(auto x : d)
		{
			if(x != 0)
				return false;
		}
		return true;
	};

	//Adding to a negative number subtracts from its magnitude and vice versa
	if(increment != negative)
	{
		size_t j = pos;
		while(d[j] == 9)
		{
			d[j] = 0;
			j ++;
		}
		d[j] ++;
	}
	else
	{
		//Is the magnitude at least the step? If any digit at or above the stepped one is nonzero, it is.
		bool enough = false;
		for(size_t j=pos; j<d.size(); j++)
		{
			if(d[j] != 0)
				enough = true;
		}

		if(enough)
		{
			size_t j = pos;
			while(d[j] == 0)
			{
				d[j] = 9;
				j ++;
			}
			d[j] --;
		}

		//Going through zero: the result is the step minus the magnitude, with the opposite sign
		else
		{
			int borrow = 0;
			for(size_t j=0; j<d.size(); j++)
			{
				int minuend = (j == pos) ? 1 : 0;
				int v = minuend - d[j] - borrow;
				borrow = (v < 0) ? 1 : 0;
				d[j] = (v + 10) % 10;
			}
			negative = !negative;
		}
	}
	if(isZero())
		negative = false;

	//Put the number back together. Keep the same number of decimal places and at least one integer digit.
	size_t top = d.size();
	while( (top > static_cast<size_t>(fracDigits) + 1) && (d[top-1] == 0) )
		top --;

	string number;
	if(negative)
		number += '-';
	else if(explicitPlus)
		number += '+';
	for(size_t j=top; j>0; j--)
	{
		number += static_cast<char>('0' + d[j-1]);
		if( (mark != string::npos) && (j-1 == static_cast<size_t>(fracDigits)) )
			number += text[mark];
	}

	newText = text.substr(0, signPos) + number + text.substr(numEnd);

	//Keep the cursor next to the same digit. Anything added or removed to the left of it moves it too.
	newCursor = cursor;
	if(cursor >= static_cast<int>(digitsStart))
		newCursor += static_cast<int>(newText.size()) - static_cast<int>(n);
	newCursor = min(max(newCursor, 0), static_cast<int>(newText.size()));

	return true;
}

/**
	@brief Formats a value like a number being edited by the user, keeping the cursor on the same digit place

	This is for when the value in a box the user is stepping with StepNumericText() is changed by something else, for
	example because an instrument limited the value to its range. Rather than switching to whatever PrettyPrint() would
	show, the value is shown with the prefix, unit and decimal mark of the text, and the cursor is put after the digit
	with the same place value as before, so the next step is the same size. Leading zeros are added if the value doesn't
	have a digit there: with the cursor at "1| MHz", 200 kHz becomes "0|.2 MHz", and with the cursor at "1|0 MHz",
	5 MHz becomes "0|5 MHz".

	At least as many decimal places as the text had are shown, more if the value needs them.

	@param value		Value to format
	@param text			Text being edited, in the form described in StepNumericText()
	@param cursor		Cursor position, as a byte offset into text
	@param newText		The value, formatted like text
	@param newCursor	Cursor position in newText

	@return				True if text has a number and value could be formatted like it. If false, newText and
						newCursor are not modified.
 */
bool Unit::ReformatLikeText(double value, const string& text, int cursor, string& newText, int& newCursor)
{
	auto num = FindNumber(text);
	if(num.intDigits + num.fracDigits == 0)
		return false;

	//Scale of the prefix and unit after the number, like 1e6 for " MHz"
	string suffix = text.substr(num.numEnd);
	double scale = ParseString("1" + suffix);
	if(!isfinite(scale) || (scale == 0))
		return false;
	double x = fabs(value / scale);
	if(!isfinite(x) || (x >= 1e15))
		return false;

	//Place of the digit the cursor is on, if it's in the number
	bool inNumber = (cursor <= static_cast<int>(num.numEnd));
	int exponent = CursorExponent(num, cursor);
	bool beforeDigits = (cursor <= static_cast<int>(num.digitsStart));

	//Same number of decimal places as the text, more if the value needs them, and enough to have the cursor digit
	int decimals = num.fracDigits;
	if(inNumber && (exponent < 0))
		decimals = max(decimals, -exponent);
	while(decimals < num.fracDigits + 9)
	{
		double p = pow(10, decimals);
		if(fabs(round(x * p) / p - x) <= x * 1e-7)
			break;
		decimals ++;
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "%.*f", decimals, x);
	string digits(buf);

	//Use the decimal mark of the text, whatever the C locale has
	size_t dot = digits.find_first_not_of("0123456789");
	if(dot != string::npos)
		digits[dot] = (num.mark != string::npos) ? text[num.mark] : m_decimalSeparator;
	int newInt = static_cast<int>( (dot == string::npos) ? digits.size() : dot );

	//Add leading zeros if needed so there's still a digit at the cursor's place
	int wantInt = 0;
	if(inNumber && (exponent >= 0))
		wantInt = beforeDigits ? exponent : exponent + 1;
	if(newInt < wantInt)
	{
		digits.insert(0, wantInt - newInt, '0');
		newInt = wantInt;
	}

	string sign;
	if( (value < 0) && (digits.find_first_of("123456789") != string::npos) )
		sign = "-";
	else if(num.explicitPlus)
		sign = "+";

	newText = text.substr(0, num.signPos) + sign + digits + suffix;
	int newDigitsStart = static_cast<int>(num.signPos + sign.size());
	int newNumEnd = newDigitsStart + static_cast<int>(digits.size());
	if(!inNumber)
		newCursor = newNumEnd + (cursor - static_cast<int>(num.numEnd));
	else if(cursor < static_cast<int>(num.digitsStart))
		newCursor = min(cursor, newDigitsStart);
	else if( (num.mark != string::npos) && (dot != string::npos) && (cursor == static_cast<int>(num.mark) + 1) )
		newCursor = newDigitsStart + newInt + 1;
	else if(exponent >= 0)
		newCursor = newDigitsStart + newInt - exponent;
	else
		newCursor = newDigitsStart + newInt + 1 - exponent;
	newCursor = min(max(newCursor, 0), static_cast<int>(newText.size()));

	return true;
}

/**
	@brief Checks if the rest of a string is just the unit, with no SI prefix

	Some units start with a letter that is also a prefix, like "m" for meters or "mV" for UNIT_MILLIVOLTS, so "2 m" is
	two meters rather than two millimeters.

	@param str		String being parsed
	@param start	Index of the first character after the number

	@return			True if everything from start on, ignoring trailing whitespace, is the unit's suffix
 */
bool Unit::IsUnitSuffix(const string& str, size_t start) const
{
	string suffix = GetScaling(1).suffix;
	if(suffix.empty())
		return false;

	size_t end = str.size();
	while( (end > start) && isspace(static_cast<unsigned char>(str[end-1])) )
		end --;
	return str.compare(start, end - start, suffix) == 0;
}

/**
	@brief Gets the factor between the unit shown and the unit values are stored in, like 1e15 for femtoseconds

	As a fraction mul / div, so integer parsing can apply it exactly.
 */
void Unit::GetBaseScale(int64_t& mul, int64_t& div) const
{
	mul = 1;
	div = 1;
	switch(m_type)
	{
		case Unit::UNIT_FS:
			mul = 1000000000000000LL;
			break;

		case Unit::UNIT_PM:
			mul = 1000000000000LL;
			break;

		case Unit::UNIT_MICROVOLTS:
		case Unit::UNIT_MICROHZ:
		case Unit::UNIT_MICROAMPS:
			mul = 1000000;
			break;

		case Unit::UNIT_PERCENT:
			div = 100;
			break;

		default:
			break;
	}
}

/**
	@brief Gets the scale of the SI prefix at a position in a string being parsed

	The scale is a fraction mul / div, so integer parsing can apply it exactly. Bytes use binary prefixes.

	@param str		String being parsed
	@param i		Position of the first character after the number
	@param mul		Set to the numerator of the scale
	@param div		Set to the denominator of the scale

	@return			True if there is a prefix at i (rather than just the unit, or nothing that's known)
 */
bool Unit::GetPrefixScale(const string& str, size_t i, int64_t& mul, int64_t& div) const
{
	mul = 1;
	div = 1;

	//The unit on its own (like "m" for meters) is not a prefix
	if(IsUnitSuffix(str, i))
		return false;

	int64_t k = (m_type == UNIT_BYTES) ? 1024 : 1000;
	char c = str[i];
	if(c == 'T')
		mul = k * k * k * k;
	else if(c == 'G')
		mul = k * k * k;
	else if(c == 'M')
		mul = k * k;
	else if( (c == 'K') || (c == 'k') )
		mul = k;
	else if(c == 'm')
		div = 1000LL;
	else if( (c == 'u') || (str.find("μ", i) == i) )
		div = 1000000LL;
	else if(c == 'n')
		div = 1000000000LL;
	else if(c == 'p')
		div = 1000000000000LL;
	else if(c == 'f')
		div = 1000000000000000LL;
	else
		return false;

	return true;
}

/**
	@brief Parses a string based on the supplied unit

	@param str					The string to parse
	@param useDisplayLocale		True if the string is formatted for display (user's locale)
								False if the string is formatted for serialization ("C" locale regardless of user pref)
 */
double Unit::ParseString(const string& str, bool useDisplayLocale)
{
	// Special handling for overload value
	if(str == UNIT_OVERLOAD_LABEL)
		return std::numeric_limits<double>::max();

	if(useDisplayLocale)
		SetPrintingLocale();

	double ret;

	if(m_type == UNIT_HEXNUM)
	{
		unsigned int temp = 0;
		sscanf(str.c_str(), "0x%x", &temp);
		ret = temp;
	}

	else
	{
		//Find the first non-numeric character in the string
		double scale = 1;
		for(size_t i=0; i<str.size(); i++)
		{
			char c = str[i];
			if(isspace(c) || isdigit(c) || (c == '.') || (c == ',') || (c == '-') )
				continue;

			int64_t mul;
			int64_t div;
			if(GetPrefixScale(str, i, mul, div))
				scale = static_cast<double>(mul) / div;
			break;
		}

		//Parse the base value
		sscanf(str.c_str(), "%20lf", &ret);

		//Apply a unit-specific scaling factor
		int64_t mul;
		int64_t div;
		GetBaseScale(mul, div);
		ret = ret * mul / div;

		ret *= scale;
	}

	SetDefaultLocale();
	return ret;
}

/**
	@brief Multiplies the fraction num / den by mul / div, cancelling common factors first

	This keeps the numbers small, so that (for example) "1.0004 μs" doesn't overflow by being multiplied by 1e15 for
	femtoseconds before being divided by 1e10 for the prefix and decimal places.
 */
static void MulFraction(int64_t& num, int64_t& den, int64_t mul, int64_t div)
{
	int64_t a = gcd(num, div);
	num /= a;
	div /= a;
	int64_t b = gcd(mul, den);
	mul /= b;
	den /= b;

	num *= mul;
	den *= div;
}

/**
	@brief Parses a string based on the supplied unit

	@param str					The string to parse
	@param useDisplayLocale		True if the string is formatted for display (user's locale)
								False if the string is formatted for serialization ("C" locale regardless of user pref)
 */
int64_t Unit::ParseStringInt64(const string& str, bool useDisplayLocale)
{
	if(useDisplayLocale)
		SetPrintingLocale();

	int64_t ret;

	if(m_type == UNIT_HEXNUM)
	{
		uint64_t temp = 0;
		sscanf(str.c_str(), "0x%" SCNx64, &temp);
		ret = temp;
	}

	else
	{
		//Apply unit-specific scaling factor first
		int64_t mulscale;
		int64_t divscale;
		GetBaseScale(mulscale, divscale);

		//Remove decimal separators  and find suffixes
		string sbase;
		bool foundDecimal = false;
		for(size_t i=0; i<str.size(); i++)
		{
			char c = str[i];

			if(isspace(c))
				continue;
			else if(isdigit(c))
			{
				sbase += c;
				if(foundDecimal)
					MulFraction(mulscale, divscale, 1, 10);
				continue;
			}
			else if(c == '-')
			{
				sbase += c;
				continue;
			}
			else if( (c == '.') || (c == ',') )
			{
				foundDecimal = true;
				continue;
			}

			int64_t mul;
			int64_t div;
			if(GetPrefixScale(str, i, mul, div))
				MulFraction(mulscale, divscale, mul, div);
			break;
		}

		//Parse the base value
		sscanf(sbase.c_str(), "%" SCNd64, &ret);
		ret *= mulscale;
		ret /= divscale;
	}

	SetDefaultLocale();

	return ret;
}

/**
	@brief Multiplies two units and calculates the resulting unit
 */
Unit Unit::operator*(const Unit& rhs)
{
	//Voltage times current is power
	if( ( (m_type == UNIT_VOLTS) && (rhs.m_type == UNIT_AMPS) ) ||
		( (rhs.m_type == UNIT_VOLTS) && (m_type == UNIT_AMPS) ) )
	{
		return Unit(UNIT_WATTS);
	}

	//Unknown / invalid pairing?
	//For now, just return the first unit.
	//TODO: how should we handle this
	return Unit(m_type);
}

/**
	@brief Divides two units and calculates the resulting unit
 */
Unit Unit::operator/(const Unit& rhs)
{
	//Same unit? Dimensionless ratio
	//TODO: should we output percent or counts here? or what
	if(m_type == rhs.m_type)
		return Unit(Unit::UNIT_COUNTS);

	//Ohm's law
	if( (m_type == UNIT_VOLTS) && (rhs.m_type == UNIT_OHMS) )
		return Unit(UNIT_AMPS);
	if( (m_type == UNIT_VOLTS) && (rhs.m_type == UNIT_AMPS) )
		return Unit(UNIT_OHMS);

	//Power
	if( (m_type == UNIT_WATTS) && (rhs.m_type == UNIT_AMPS) )
		return Unit(UNIT_VOLTS);
	if( (m_type == UNIT_WATTS) && (rhs.m_type == UNIT_VOLTS) )
		return Unit(UNIT_AMPS);

	//Unknown / invalid pairing?
	//For now, just return the first unit.
	//TODO: how should we handle this
	return Unit(m_type);
}

/**
	@brief Initialize the locales for pretty-printing and file interchange
 */
void Unit::InitializeLocales()
{
	LogDebug("Initializing locales\n");
	LogIndenter li;

	#ifdef _WIN32
		setlocale(LC_NUMERIC, "");
		m_slocale = setlocale(LC_NUMERIC, nullptr);
	#else
		m_locale = newlocale(LC_NUMERIC_MASK, "", 0);
		if(!m_locale)
			LogError("Failed to create UI locale\n");
		m_defaultLocale = newlocale(LC_NUMERIC_MASK, "C", 0);
		if(!m_defaultLocale)
			LogError("Failed to create default locale\n");
	#endif

	//Figure out the decimal separator in use
	SetPrintingLocale();
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%3.1f", 0.0);
	m_decimalSeparator = tmp[1];
	LogDebug("Decimal separator is %c\n", m_decimalSeparator);
	SetDefaultLocale();
}

/**
	@brief Sets the current locale to the user's selected LC_NUMERIC for printing numbers for display
 */
void Unit::SetPrintingLocale()
{
	#ifdef _WIN32
		setlocale(LC_NUMERIC, m_slocale.c_str());
	#else
		uselocale(m_locale);
	#endif
}

/**
	@brief Sets the current locale to "C" for interchange
 */
void Unit::SetDefaultLocale()
{
	#ifdef _WIN32
		setlocale(LC_NUMERIC, "C");
	#else
		uselocale(m_defaultLocale);
	#endif
}
