/*
*
*    This program is free software; you can redistribute it and/or modify it
*    under the terms of the GNU General Public License as published by the
*    Free Software Foundation; either version 2 of the License, or (at
*    your option) any later version.
*
*    This program is distributed in the hope that it will be useful, but
*    WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*    General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include "base64.h"

namespace base64 {

// Returns the expected buffer size that should be passed to Encode.
// cubData - Size of data to encode
uint32_t GetEncodeMaxOutput(uint32_t cubData)
{
	// 4 chars per 3-byte group + terminating null
	return (cubData + 2) / 3 * 4 + 1;
}

// Returns the expected buffer size that should be passed to Decode
// cubData - Size of encoded string to decode
uint32_t GetDecodeMaxOutput(uint32_t cubData)
{
	// 3 bytes per 4-char group, round up any partial group
	return ((cubData + 3) / 4) * 3 + 1;
}

// Encodes a block of data. (Binary -> text representation.)
// The output is null-terminated and can be treated as a string.
// pubData         - Data to encode
// cubData         - Size of data to encode
// pchEncodedData  - Pointer to string buffer to store output in
// pcchEncodedData - Pointer to size of pchEncodedData buffer; adjusted to number of characters written (before NULL)
//
// Note: if pchEncodedData is NULL and *pcchEncodedData is zero, *pcchEncodedData is filled with the actual required length for output.
// A simpler approximation for maximum output size is (cubData * 4 / 3) + 5
bool Encode(const uint8_t *pubData, uint32_t cubData, char *pchEncodedData, uint32_t *pcchEncodedData, bool bURLSafe)
{
	if (!pchEncodedData)
	{
		assert(*pcchEncodedData == 0 && "NULL output buffer with non-zero size passed to base64::Encode");
		*pcchEncodedData = GetEncodeMaxOutput(cubData);
		return true;
	}

	const uint8_t *pubDataEnd = pubData + cubData;
	char *pchEncodedDataStart = pchEncodedData;

	const char *const pszBase64Chars = bURLSafe ?
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
			:
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	uint32_t cchEncodedData = *pcchEncodedData;
	if (cchEncodedData == 0)
		goto out_of_space;

	cchEncodedData--; // decrement for the terminating null so we don't forget about it

	// input 3 x 8-bit, output 4 x 6-bit
	while (pubDataEnd - pubData >= 3)
	{
		if (cchEncodedData < 4)
		{
			goto out_of_space;
		}

		uint32_t un24BitsData;
		un24BitsData  = (uint32_t)pubData[0] << 16;
		un24BitsData |= (uint32_t)pubData[1] << 8;
		un24BitsData |= (uint32_t)pubData[2];
		pubData += 3;

		pchEncodedData[0] = pszBase64Chars[(un24BitsData >> 18) & 63];
		pchEncodedData[1] = pszBase64Chars[(un24BitsData >> 12) & 63];
		pchEncodedData[2] = pszBase64Chars[(un24BitsData >>  6) & 63];
		pchEncodedData[3] = pszBase64Chars[(un24BitsData     ) & 63];
		pchEncodedData += 4;
		cchEncodedData -= 4;
	}

	// Clean up remaining 1 or 2 bytes of input, pad output with '='
	if (pubData != pubDataEnd)
	{
		if (cchEncodedData < 4)
			goto out_of_space;

		uint32_t un24BitsData;
		un24BitsData = (uint32_t)pubData[0] << 16;
		if (pubData + 1 != pubDataEnd)
		{
			un24BitsData |= (uint32_t)pubData[1] << 8;
		}

		pchEncodedData[0] = pszBase64Chars[(un24BitsData >> 18) & 63];
		pchEncodedData[1] = pszBase64Chars[(un24BitsData >> 12) & 63];
        pchEncodedData[2] = (pubData + 1 != pubDataEnd) ? pszBase64Chars[(un24BitsData >> 6) & 63] : (bURLSafe ? '\0' : '=');
		pchEncodedData[3] = bURLSafe ? '\0' : '=';
		pchEncodedData += 4;
		cchEncodedData -= 4;
	}

	*pchEncodedData = 0;
	*pcchEncodedData = (uint32_t)(pchEncodedData - pchEncodedDataStart);
	return true;

out_of_space:
	*pchEncodedData = 0;
	*pcchEncodedData = GetEncodeMaxOutput(cubData);
	assert(false && "Base64Encode: insufficient output buffer (up to n*4/3+5 bytes required)");
	return false;
}

// Decodes a block of data. (Text -> binary representation.)
// pchEncodedData  - base64-encoded string, null terminated
// cchEncodedData  - maximum length of string unless a null is encountered first
// pubDecodedData  - Pointer to buffer to store output in
// pcubDecodedData - Pointer to variable that contains size of
//                   output buffer. At exit, is filled in with actual size of decoded data.
//
// Note: if NULL is passed as the output buffer and *pcubDecodedData is zero, the function
// will calculate the actual required size and place it in *pcubDecodedData. A simpler upper
// bound on the required size is (strlen(pchEncodedData)*3/4 + 2).
bool Decode(const char *pchEncodedData, uint32_t cchEncodedData, uint8_t *pubDecodedData, uint32_t *pcubDecodedData, bool bURLSafe)
{
	uint32_t cubDecodedData = *pcubDecodedData;
	uint32_t cubDecodedDataOrig = cubDecodedData;

	if (pubDecodedData == NULL)
	{
		assert(*pcubDecodedData == 0 && "NULL output buffer with non-zero size passed to base64::Decode");
		cubDecodedDataOrig = cubDecodedData = ~0u;
	}

	// valid base64 character range: '+' (0x2B) to 'z' (0x7A)
	// table entries are 0-63, -1 for invalid entries, -2 for '='
	static const signed char rgchInvBase64[] =
	{
		62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
		-1, -1, -1, -2, -1, -1, -1,  0,  1,  2,  3,  4,  5,  6,  7,
		 8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
		23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
		47, 48, 49, 50, 51
	};

	const uint32_t nInvBaseLen = sizeof(rgchInvBase64) / sizeof(rgchInvBase64[0]);
	uint32_t un24BitsWithSentinel = 1;
	while (cchEncodedData-- > 0)
	{
		char c = *pchEncodedData++;

		if ((uint8_t)(c - 0x2B) >= nInvBaseLen)
		{
			if (c == '\0')
				break;

			goto decode_failed;
		}

		if (bURLSafe)
		{
			if (c == '-') c = '+';
			else if (c == '_') c = '/';
		}

		c = rgchInvBase64[(uint8_t)(c - 0x2B)];
		if ((signed char)c < 0)
		{
			if ((signed char)c == -2) // -2 -> terminating '='
				break;

			goto decode_failed;
		}

		un24BitsWithSentinel <<= 6;
		un24BitsWithSentinel |= c;

		if (un24BitsWithSentinel & (1<<24))
		{
			if (cubDecodedData < 3) // out of space? go to final write logic
			{
				break;
			}

			if (pubDecodedData)
			{
				pubDecodedData[0] = (uint8_t)(un24BitsWithSentinel >> 16);
				pubDecodedData[1] = (uint8_t)(un24BitsWithSentinel >> 8);
				pubDecodedData[2] = (uint8_t)(un24BitsWithSentinel);
				pubDecodedData += 3;
			}

			cubDecodedData -= 3;
			un24BitsWithSentinel = 1;
		}
	}

	// If un24BitsWithSentinel contains data, output the remaining full bytes
	if (un24BitsWithSentinel >= (1 << 6))
	{
		// Possibilities are 3, 2, 1, or 0 full output bytes.
		int nWriteBytes = 3;
		while (un24BitsWithSentinel < (1 << 24))
		{
			nWriteBytes--;
			un24BitsWithSentinel <<= 6;
		}

		// Write completed bytes to output
		while (nWriteBytes-- > 0)
		{
			if (cubDecodedData == 0)
			{
				assert(false && "base64::Decode: insufficient output buffer (up to n*3/4+2 bytes required)");
				goto decode_failed;
			}

			if (pubDecodedData)
			{
				*pubDecodedData++ = (uint8_t)(un24BitsWithSentinel >> 16);
			}

			cubDecodedData--;
			un24BitsWithSentinel <<= 8;
		}
	}

	*pcubDecodedData = cubDecodedDataOrig - cubDecodedData;
	return true;

decode_failed:
	*pcubDecodedData = cubDecodedDataOrig - cubDecodedData;
	return false;
}

#if defined(UTLBUFFER_H)

// Encode/decode helpers using CUtlBuffer
bool DecodeToBuf(const char *pszEncoded, uint32_t cbEncoded, CUtlBuffer &buf)
{
	uint32_t cubDecodeSize = GetDecodeMaxOutput(cbEncoded);
	buf.EnsureCapacity(cubDecodeSize);
	if (!Decode(pszEncoded, cbEncoded, (uint8_t *)buf.Base(), &cubDecodeSize))
		return false;

	buf.SeekPut(CUtlBuffer::SEEK_HEAD, cubDecodeSize);
	return true;
}

bool EncodeToBuf(uint8_t *pubData, uint32_t cubData, CUtlBuffer &buf)
{
	uint32_t cubEncodeSize = GetEncodeMaxOutput(cubData);
	buf.EnsureCapacity(cubEncodeSize);
	if (!Encode(pubData, cubData, (char *)buf.Base(), &cubEncodeSize))
		return false;

	buf.SeekPut(CUtlBuffer::SEEK_HEAD, cubEncodeSize);
	return true;
}
#endif

} // namespace base64
