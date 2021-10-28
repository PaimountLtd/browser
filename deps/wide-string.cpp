/******************************************************************************
 Copyright (C) 2018 by Hugh Bailey ("Jim") <jim@obsproject.com>

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/

#include "wide-string.hpp"
#include <string.h>
// #include <util/platform.h>

#ifdef _WIN32
	#include <wchar.h>
	// #include "utf8.h"
	#include <windows.h>
	// #include "c99defs.h"
#endif

using namespace std;

static inline bool has_utf8_bom(const char *in_char)
{
	uint8_t *in = (uint8_t *)in_char;
	return (in && in[0] == 0xef && in[1] == 0xbb && in[2] == 0xbf);
}

size_t utf8_to_wchar(const char *in, size_t insize, wchar_t *out,
		     size_t outsize, int flags)
{
	int i_insize = (int)insize;
	int ret;

	if (i_insize == 0)
		i_insize = (int)strlen(in);

	/* prevent bom from being used in the string */
	if (has_utf8_bom(in)) {
		if (i_insize >= 3) {
			in += 3;
			i_insize -= 3;
		}
	}

	ret = MultiByteToWideChar(CP_UTF8, 0, in, i_insize, out, (int)outsize);

	return (ret > 0) ? (size_t)ret : 0;
}

size_t os_utf8_to_wcs(const char *str, size_t len, wchar_t *dst,
		      size_t dst_size)
{
	size_t in_len;
	size_t out_len;

	if (!str)
		return 0;

	in_len = len ? len : strlen(str);
	out_len = dst ? (dst_size - 1) : utf8_to_wchar(str, in_len, NULL, 0, 0);

	if (dst) {
		if (!dst_size)
			return 0;

		if (out_len)
			out_len =
				utf8_to_wchar(str, in_len, dst, out_len + 1, 0);

		dst[out_len] = 0;
	}

	return out_len;
}

wstring to_wide(const char *utf8)
{
	if (!utf8 || !*utf8)
		return wstring();

	size_t isize = strlen(utf8);
	size_t osize = os_utf8_to_wcs(utf8, isize, nullptr, 0);

	if (!osize)
		return wstring();

	wstring wide;
	wide.resize(osize);
	os_utf8_to_wcs(utf8, isize, &wide[0], osize + 1);
	return wide;
}

wstring to_wide(const std::string &utf8)
{
	if (utf8.empty())
		return wstring();

	size_t osize = os_utf8_to_wcs(utf8.c_str(), utf8.size(), nullptr, 0);

	if (!osize)
		return wstring();

	wstring wide;
	wide.resize(osize);
	os_utf8_to_wcs(utf8.c_str(), utf8.size(), &wide[0], osize + 1);
	return wide;
}
