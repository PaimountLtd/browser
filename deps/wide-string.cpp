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

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#endif

using namespace std;

#ifdef _WIN32

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

#else

#define _NXT 0x80
#define _SEQ2 0xc0
#define _SEQ3 0xe0
#define _SEQ4 0xf0
#define _SEQ5 0xf8
#define _SEQ6 0xfc

#define _BOM 0xfeff

static int wchar_forbidden(wchar_t sym);
static int utf8_forbidden(unsigned char octet);

static int wchar_forbidden(wchar_t sym)
{
	/* Surrogate pairs */
	if (sym >= 0xd800 && sym <= 0xdfff)
		return -1;

	return 0;
}

static int utf8_forbidden(unsigned char octet)
{
	switch (octet) {
	case 0xc0:
	case 0xc1:
	case 0xf5:
	case 0xff:
		return -1;
	}

	return 0;
}

/*
 * DESCRIPTION
 *	This function translates UTF-8 string into UCS-4 string (all symbols
 *	will be in local machine byte order).
 *
 *	It takes the following arguments:
 *	in	- input UTF-8 string. It can be null-terminated.
 *	insize	- size of input string in bytes.  If insize is 0,
 *	        function continues until a null terminator is reached.
 *	out	- result buffer for UCS-4 string. If out is NULL,
 *		function returns size of result buffer.
 *	outsize - size of out buffer in wide characters.
 *
 * RETURN VALUES
 *	The function returns size of result buffer (in wide characters).
 *	Zero is returned in case of error.
 *
 * CAVEATS
 *	1. If UTF-8 string contains zero symbols, they will be translated
 *	   as regular symbols.
 *	2. If UTF8_IGNORE_ERROR or UTF8_SKIP_BOM flag is set, sizes may vary
 *	   when `out' is NULL and not NULL. It's because of special UTF-8
 *	   sequences which may result in forbidden (by RFC3629) UNICODE
 *	   characters.  So, the caller must check return value every time and
 *	   not prepare buffer in advance (\0 terminate) but after calling this
 *	   function.
 */
size_t utf8_to_wchar(const char *in, size_t insize, wchar_t *out,
		     size_t outsize, int flags)
{
	unsigned char *p, *lim;
	wchar_t *wlim, high;
	size_t n, total, i, n_bits;

	if (in == NULL || (outsize == 0 && out != NULL))
		return 0;

	total = 0;
	p = (unsigned char *)in;
	lim = (insize != 0) ? (p + insize) : (unsigned char *)-1;
	wlim = out == NULL ? NULL : out + outsize;

	for (; p < lim; p += n) {
		if (!*p)
			break;

		if (utf8_forbidden(*p) != 0 && (flags & UTF8_IGNORE_ERROR) == 0)
			return 0;

		/*
		 * Get number of bytes for one wide character.
		 */
		n = 1; /* default: 1 byte. Used when skipping bytes. */
		if ((*p & 0x80) == 0)
			high = (wchar_t)*p;
		else if ((*p & 0xe0) == _SEQ2) {
			n = 2;
			high = (wchar_t)(*p & 0x1f);
		} else if ((*p & 0xf0) == _SEQ3) {
			n = 3;
			high = (wchar_t)(*p & 0x0f);
		} else if ((*p & 0xf8) == _SEQ4) {
			n = 4;
			high = (wchar_t)(*p & 0x07);
		} else if ((*p & 0xfc) == _SEQ5) {
			n = 5;
			high = (wchar_t)(*p & 0x03);
		} else if ((*p & 0xfe) == _SEQ6) {
			n = 6;
			high = (wchar_t)(*p & 0x01);
		} else {
			if ((flags & UTF8_IGNORE_ERROR) == 0)
				return 0;
			continue;
		}

		/* does the sequence header tell us truth about length? */
		if ((size_t)(lim - p) <= n - 1) {
			if ((flags & UTF8_IGNORE_ERROR) == 0)
				return 0;
			n = 1;
			continue; /* skip */
		}

		/*
		 * Validate sequence.
		 * All symbols must have higher bits set to 10xxxxxx
		 */
		if (n > 1) {
			for (i = 1; i < n; i++) {
				if ((p[i] & 0xc0) != _NXT)
					break;
			}
			if (i != n) {
				if ((flags & UTF8_IGNORE_ERROR) == 0)
					return 0;
				n = 1;
				continue; /* skip */
			}
		}

		total++;

		if (out == NULL)
			continue;

		if (out >= wlim)
			return 0; /* no space left */

		*out = 0;
		n_bits = 0;
		for (i = 1; i < n; i++) {
			*out |= (wchar_t)(p[n - i] & 0x3f) << n_bits;
			n_bits += 6; /* 6 low bits in every byte */
		}
		*out |= high << n_bits;

		if (wchar_forbidden(*out) != 0) {
			if ((flags & UTF8_IGNORE_ERROR) == 0)
				return 0; /* forbidden character */
			else {
				total--;
				out--;
			}
		} else if (*out == _BOM && (flags & UTF8_SKIP_BOM) != 0) {
			total--;
			out--;
		}

		out++;
	}

	return total;
}

/*
 * DESCRIPTION
 *	This function translates UCS-4 symbols (given in local machine
 *	byte order) into UTF-8 string.
 *
 *	It takes the following arguments:
 *	in	- input unicode string. It can be null-terminated.
 *	insize	- size of input string in wide characters.  If insize is 0,
 *	        function continues until a null terminator is reaches.
 *	out	- result buffer for utf8 string. If out is NULL,
 *		function returns size of result buffer.
 *	outsize - size of result buffer.
 *
 * RETURN VALUES
 *	The function returns size of result buffer (in bytes). Zero is returned
 *	in case of error.
 *
 * CAVEATS
 *	If UCS-4 string contains zero symbols, they will be translated
 *	as regular symbols.
 */
size_t wchar_to_utf8(const wchar_t *in, size_t insize, char *out,
		     size_t outsize, int flags)
{
	wchar_t *w, *wlim, ch = 0;
	unsigned char *p, *lim, *oc;
	size_t total, n;

	if (in == NULL || (outsize == 0 && out != NULL))
		return 0;

	w = (wchar_t *)in;
	wlim = (insize != 0) ? (w + insize) : (wchar_t *)-1;
	p = (unsigned char *)out;
	lim = out == NULL ? NULL : p + outsize;
	total = 0;

	for (; w < wlim; w++) {
		if (!*w)
			break;

		if (wchar_forbidden(*w) != 0) {
			if ((flags & UTF8_IGNORE_ERROR) == 0)
				return 0;
			else
				continue;
		}

		if (*w == _BOM && (flags & UTF8_SKIP_BOM) != 0)
			continue;

		if (*w < 0) {
			if ((flags & UTF8_IGNORE_ERROR) == 0)
				return 0;
			continue;
		} else if (*w <= 0x0000007f)
			n = 1;
		else if (*w <= 0x000007ff)
			n = 2;
		else if (*w <= 0x0000ffff)
			n = 3;
		else if (*w <= 0x001fffff)
			n = 4;
		else if (*w <= 0x03ffffff)
			n = 5;
		else /* if (*w <= 0x7fffffff) */
			n = 6;

		total += n;

		if (out == NULL)
			continue;

		if ((size_t)(lim - p) <= n - 1)
			return 0; /* no space left */

		ch = *w;
		oc = (unsigned char *)&ch;
		switch (n) {
		case 1:
			*p = oc[0];
			break;

		case 2:
			p[1] = _NXT | (oc[0] & 0x3f);
			p[0] = _SEQ2 | (oc[0] >> 6) | ((oc[1] & 0x07) << 2);
			break;

		case 3:
			p[2] = _NXT | (oc[0] & 0x3f);
			p[1] = _NXT | (oc[0] >> 6) | ((oc[1] & 0x0f) << 2);
			p[0] = _SEQ3 | ((oc[1] & 0xf0) >> 4);
			break;

		case 4:
			p[3] = _NXT | (oc[0] & 0x3f);
			p[2] = _NXT | (oc[0] >> 6) | ((oc[1] & 0x0f) << 2);
			p[1] = _NXT | ((oc[1] & 0xf0) >> 4) |
			       ((oc[2] & 0x03) << 4);
			p[0] = _SEQ4 | ((oc[2] & 0x1f) >> 2);
			break;

		case 5:
			p[4] = _NXT | (oc[0] & 0x3f);
			p[3] = _NXT | (oc[0] >> 6) | ((oc[1] & 0x0f) << 2);
			p[2] = _NXT | ((oc[1] & 0xf0) >> 4) |
			       ((oc[2] & 0x03) << 4);
			p[1] = _NXT | (oc[2] >> 2);
			p[0] = _SEQ5 | (oc[3] & 0x03);
			break;

		case 6:
			p[5] = _NXT | (oc[0] & 0x3f);
			p[4] = _NXT | (oc[0] >> 6) | ((oc[1] & 0x0f) << 2);
			p[3] = _NXT | (oc[1] >> 4) | ((oc[2] & 0x03) << 4);
			p[2] = _NXT | (oc[2] >> 2);
			p[1] = _NXT | (oc[3] & 0x3f);
			p[0] = _SEQ6 | ((oc[3] & 0x40) >> 6);
			break;
		}

		/*
		 * NOTE: do not check here for forbidden UTF-8 characters.
		 * They cannot appear here because we do proper conversion.
		 */

		p += n;
	}

	return total;
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

#endif

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
