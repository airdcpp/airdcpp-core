/*
 * Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdinc.h"
#include "Text.h"

#ifdef _WIN32
#include "w.h"
#else
#include <errno.h>
#include <iconv.h>
#include <langinfo.h>

#ifndef ICONV_CONST
 #define ICONV_CONST
#endif

#endif

#include "Util.h"

namespace dcpp {

namespace Text {

const string utf8 = "utf-8"; // optimization
string systemCharset;

void initialize() {
	setlocale(LC_ALL, "");

#ifdef _WIN32
	char *ctype = setlocale(LC_CTYPE, NULL);
	if(ctype) {
		systemCharset = string(ctype);
	} else {
		dcdebug("Unable to determine the program's locale");
	}
#else
	systemCharset = string(nl_langinfo(CODESET));
#endif
}

#ifdef _WIN32
int getCodePage(const string& charset) {
	if(charset.empty() || Util::stricmp(charset, systemCharset) == 0)
		return CP_ACP;

	string::size_type pos = charset.find('.');
	if(pos == string::npos)
		return CP_ACP;

	return atoi(charset.c_str() + pos + 1);
}
#endif

bool isAscii(const char* str) noexcept {
	for(const uint8_t* p = (const uint8_t*)str; *p; ++p) {
		if(*p & 0x80)
			return false;
	}
	return true;
}

int utf8ToWc(const char* str, wchar_t& c) {
	const auto c0 = static_cast<uint8_t>(str[0]);
	const auto bytes = 2 + !!(c0 & 0x20) + ((c0 & 0x30) == 0x30);

	if ((c0 & 0xc0) == 0xc0) {                  // 11xx xxxx
												// # bytes of leading 1's; check for 0 next
		const auto check_bit = 1 << (7 - bytes);
		if (c0 & check_bit)
			return -1;

		c = (check_bit - 1) & c0;

		// 2-4 total, or 1-3 additional, bytes
		// Can't run off end of str so long as has sub-0x80-terminator
		for (auto i = 1; i < bytes; ++i) {
			const auto ci = static_cast<uint8_t>(str[i]);
			if ((ci & 0xc0) != 0x80)
				return -i;
			c = (c << 6) | (ci & 0x3f);
		}

		// Invalid UTF-8 code points
		if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
			// "REPLACEMENT CHARACTER": used to replace an incoming character
			// whose value is unknown or unrepresentable in Unicode
			c = 0xfffd;
			return -bytes;
		}

		return bytes;
	}
	else if ((c0 & 0x80) == 0) {             // 0xxx xxxx
		c = static_cast<unsigned char>(str[0]);
		return 1;
	}
	else {                                   // 10xx xxxx
		return -1;
	}
	dcassert(0);
}

void wcToUtf8(wchar_t c, string& str) {
	// https://tools.ietf.org/html/rfc3629#section-3
	if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
		// Invalid UTF-8 code point
		// REPLACEMENT CHARACTER: http://www.fileformat.info/info/unicode/char/0fffd/index.htm
		wcToUtf8(0xfffd, str);
	} else if (c >= 0x10000) {
		str += (char)(0x80 | 0x40 | 0x20 | 0x10 | (c >> 18));
		str += (char)(0x80 | ((c >> 12) & 0x3f));
		str += (char)(0x80 | ((c >> 6) & 0x3f));
		str += (char)(0x80 | (c & 0x3f));
	} else if (c >= 0x0800) {
		str += (char)(0x80 | 0x40 | 0x20 | (c >> 12));
		str += (char)(0x80 | ((c >> 6) & 0x3f));
		str += (char)(0x80 | (c & 0x3f));
	} else if (c >= 0x0080) {
		str += (char)(0x80 | 0x40 | (c >> 6));
		str += (char)(0x80 | (c & 0x3f));
	} else {
		str += (char)c;
	}
}

wstring acpToWide(const string& str, const string& fromCharset) noexcept {
	if(str.empty())
		return Util::emptyStringW;
#ifdef _WIN32
	int n = MultiByteToWideChar(getCodePage(fromCharset), MB_PRECOMPOSED, str.c_str(), (int)str.length(), NULL, 0);
	if(n == 0) {
		return Util::emptyStringW;
	}

	wstring tmp;
	tmp.resize(n);
	n = MultiByteToWideChar(getCodePage(fromCharset), MB_PRECOMPOSED, str.c_str(), (int)str.length(), &tmp[0], n);
	if(n == 0) {
		return Util::emptyStringW;
	}
	return tmp;
#else
	size_t rv;
	wchar_t wc;
	const char *src = str.c_str();
	size_t n = str.length() + 1;

	wstring tmp;
	tmp.reserve(n);

	while(n > 0) {
		rv = mbrtowc(&wc, src, n, NULL);
		if(rv == 0 || rv == (size_t)-2) {
			break;
		} else if(rv == (size_t)-1) {
			tmp.push_back(L'_');
			++src;
			--n;
		} else {
			tmp.push_back(wc);
			src += rv;
			n -= rv;
		}
	}
	return tmp;
#endif
}

string wideToUtf8(const wstring& str) noexcept {
	if(str.empty()) {
		return Util::emptyString;
	}

	string tgt;
	string::size_type n = str.length();
	for(string::size_type i = 0; i < n; ++i) {
		wcToUtf8(str[i], tgt);
	}
	return tgt;
}

string wideToAcp(const wstring& str, const string& toCharset) noexcept {
	if(str.empty())
		return Util::emptyString;

#ifdef _WIN32
	int n = WideCharToMultiByte(getCodePage(toCharset), 0, str.c_str(), (int)str.length(), NULL, 0, NULL, NULL);
	if(n == 0) {
		return Util::emptyString;
	}

	string tmp;
	tmp.resize(n);
	n = WideCharToMultiByte(getCodePage(toCharset), 0, str.c_str(), (int)str.length(), &tmp[0], n, NULL, NULL);
	if(n == 0) {
		return Util::emptyString;
	}
	return tmp;
#else
	const wchar_t* src = str.c_str();
	int n = wcsrtombs(NULL, &src, 0, NULL);
	if(n < 1) {
		return Util::emptyString;
	}
	src = str.c_str();

	string tmp;
	tmp.resize(n);
	n = wcsrtombs(&tmp[0], &src, n, NULL);
	if(n < 1) {
		return Util::emptyString;
	}
	return tmp;
#endif
}

string sanitizeUtf8(const string& str) noexcept {
	return wideToUtf8(utf8ToWide(str));
}

bool validateUtf8(const string& str) noexcept {
	string::size_type i = 0;
	while(i < str.length()) {
		wchar_t dummy = 0;
		int j = utf8ToWc(&str[i], dummy);
		if(j < 0)
			return false;
		i += j;
	}
	return true;
}

wstring utf8ToWide(const string& str) noexcept {
	wstring tgt;
	tgt.reserve(str.length());
	string::size_type n = str.length();
	for(string::size_type i = 0; i < n; ) {
		wchar_t c = 0;
		int x = utf8ToWc(str.c_str() + i, c);
		if(x < 0) {
			tgt += '_';
			i += abs(x);
		} else {
			i += x;
			tgt += c;
		}
	}
	return tgt;
}

wchar_t toLower(wchar_t c) noexcept {
#ifdef _WIN32
	return LOWORD(CharLowerW(reinterpret_cast<LPWSTR>(MAKELONG(c, 0))));
#else
	return (wchar_t)towlower(c);
#endif
}

wstring toLower(const wstring& str) noexcept {
	if(str.empty())
		return Util::emptyStringW;

	wstring tmp;
	tmp.reserve(str.length());
	for(auto& i: str) {
		tmp += toLower(i);
	}
	return tmp;
}

bool isLower(const string& str) noexcept {
	return compare(str, toLower(str)) == 0;
}

string toLower(const string& str) noexcept {
	if(str.empty())
		return Util::emptyString;

	string tmp;
	tmp.reserve(str.length());
	const char* end = &str[0] + str.length();
	for(const char* p = &str[0]; p < end;) {
		wchar_t c = 0;
		int n = utf8ToWc(p, c);
		if(n < 0) {
			tmp += '_';
			p += abs(n);
		} else {
			p += n;
			wcToUtf8(toLower(c), tmp);
		}
	}
	return tmp;
}

string toUtf8(const string& str, const string& fromCharset) noexcept {
	if(str.empty()) {
		return str;
	}

#ifdef _WIN32
	if (fromCharset == utf8) {
		return str;
	}

	return acpToUtf8(str, fromCharset);
#else
	return convert(str, fromCharset, utf8);
#endif
}

string fromUtf8(const string& str, const string& toCharset) noexcept {
	if(str.empty()) {
		return str;
	}

#ifdef _WIN32
	if (toCharset == utf8) {
		return str;
	}

	return utf8ToAcp(str, toCharset);
#else
	return convert(str, utf8, toCharset);
#endif
}

#ifndef _WIN32
string convert(const string& str, const string& fromCharset, const string& toCharset) noexcept {
	if(str.empty())
		return str;
	// Initialize the converter
	iconv_t cd = iconv_open(toCharset.c_str(), fromCharset.c_str());
	if (cd == (iconv_t)-1) {
		dcdebug("Unknown conversion from %s to %s\n", fromCharset.c_str(), toCharset.c_str());
		return Util::emptyString;
	}

	size_t rv;
	size_t len = str.length() * 2; // optimization
	size_t inleft = str.length();
	size_t outleft = len;

	string tmp;
	tmp.resize(len);
	const char *inbuf = str.data();
	char *outbuf = (char *)tmp.data();

	while(inleft > 0) {
		rv = iconv(cd, (ICONV_CONST char **)&inbuf, &inleft, &outbuf, &outleft);
		if(rv == (size_t)-1) {
			size_t used = outbuf - tmp.data();
			if(errno == E2BIG) {
				len *= 2;
				tmp.resize(len);
				outbuf = (char *)tmp.data() + used;
				outleft = len - used;
			} else if(errno == EILSEQ) {
				++inbuf;
				--inleft;
				tmp[used] = '_';
			} else {
				tmp.replace(used, inleft, string(inleft, '_'));
				inleft = 0;
			}
		}
	}

	iconv_close(cd);
	if(outleft > 0) {
		tmp.resize(len - outleft);
	}

	return tmp;
}
#endif
}

string Text::toDOS(string tmp) noexcept {
	if(tmp.empty())
		return Util::emptyString;

	if(tmp[0] == '\r' && (tmp.size() == 1 || tmp[1] != '\n')) {
		tmp.insert(1, "\n");
	}
	for(string::size_type i = 1; i < tmp.size() - 1; ++i) {
		if(tmp[i] == '\r' && tmp[i+1] != '\n') {
			// Mac ending
			tmp.insert(i+1, "\n");
			i++;
		} else if(tmp[i] == '\n' && tmp[i-1] != '\r') {
			// Unix encoding
			tmp.insert(i, "\r");
			i++;
		}
	}
	return tmp;
}

wstring Text::toDOS(wstring tmp) noexcept {
	if(tmp.empty())
		return Util::emptyStringW;

	if(tmp[0] == L'\r' && (tmp.size() == 1 || tmp[1] != L'\n')) {
		tmp.insert(1, L"\n");
	}
	for(string::size_type i = 1; i < tmp.size() - 1; ++i) {
		if(tmp[i] == L'\r' && tmp[i+1] != L'\n') {
			// Mac ending
			tmp.insert(i+1, L"\n");
			i++;
		} else if(tmp[i] == L'\n' && tmp[i-1] != L'\r') {
			// Unix encoding
			tmp.insert(i, L"\r");
			i++;
		}
	}
	return tmp;
}

bool Text::isSeparator(wchar_t c) noexcept {
	if (c > 127) {
		return false;
	}

	return isSeparator(static_cast<char>(c));
}

} // namespace dcpp
