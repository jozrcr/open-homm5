// The "languages" mod's work on bytes: paths, text encodings, zip directories.
// No game needed (src/hook/test/languages_test.cpp).
#include "languages.h"

#include <string.h>

namespace mods {
namespace lang {
namespace {

DWORD U32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<DWORD>(p[3]) << 24);
}

WORD U16(const unsigned char *p)
{
	return static_cast<WORD>(p[0] | (p[1] << 8));
}

}  // namespace

std::string NormalizePath(const char *begin, const char *end)
{
	while (begin < end && (*begin == '/' || *begin == '\\'))
		++begin;
	std::string s(begin, end);
	for (char &c : s) {
		if (c == '\\')
			c = '/';
		else if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return s;
}

bool IsTextPath(const char *begin, const char *end)
{
	return end - begin > 4 && _strnicmp(end - 4, ".txt", 4) == 0;
}

bool ToGameText(const std::string &raw, std::string *out)
{
	const unsigned char *b = reinterpret_cast<const unsigned char *>(raw.data());
	const size_t n = raw.size();
	if (n >= 2 && b[0] == 0xff && b[1] == 0xfe) {  // the game's own form
		out->assign(raw, 0, n & ~static_cast<size_t>(1));
		return true;
	}
	if (n >= 2 && b[0] == 0xfe && b[1] == 0xff)
		return false;  // UTF-16 big endian: the game would show garbage
	if (n >= 2 && b[0] != 0 && b[1] == 0) {  // UTF-16 LE without its mark
		out->assign("\xff\xfe", 2);
		out->append(raw, 0, n & ~static_cast<size_t>(1));
		return true;
	}
	// UTF-8, with or without its mark
	const size_t start = n >= 3 && b[0] == 0xef && b[1] == 0xbb && b[2] == 0xbf ? 3 : 0;
	std::wstring w;
	if (n > start) {
		const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data() + start,
		                                    static_cast<int>(n - start), nullptr, 0);
		if (len <= 0)
			return false;
		w.resize(len);
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data() + start, static_cast<int>(n - start), &w[0], len);
	}
	std::wstring crlf;
	crlf.reserve(w.size() + w.size() / 16);
	for (size_t i = 0; i < w.size(); ++i) {
		if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r'))
			crlf += L'\r';
		crlf += w[i];
	}
	out->assign("\xff\xfe", 2);
	out->append(reinterpret_cast<const char *>(crlf.data()), crlf.size() * sizeof(wchar_t));
	return true;
}

bool FindDirectory(const unsigned char *tail, size_t size, DWORD *dir_offset, DWORD *dir_size)
{
	if (size < 22)
		return false;
	for (size_t at = size - 22 + 1; at-- > 0;) {
		if (U32(tail + at) == 0x06054b50) {
			*dir_size = U32(tail + at + 12);
			*dir_offset = U32(tail + at + 16);
			return true;
		}
	}
	return false;
}

void ReadDirectory(const unsigned char *dir, size_t size, std::vector<ZipEntry> *out)
{
	// The entry count in the end record has 16 bits only: walk the directory by its size.
	for (size_t p = 0; p + 46 <= size && U32(dir + p) == 0x02014b50;) {
		const unsigned char *e = dir + p;
		const WORD name_len = U16(e + 28);
		const size_t next = p + 46 + name_len + U16(e + 30) + U16(e + 32);
		if (next > size)
			break;
		const char *name = reinterpret_cast<const char *>(e + 46);
		const bool folder = name_len > 0 && (name[name_len - 1] == '/' || name[name_len - 1] == '\\');
		if (!folder && name_len > 0)
			out->push_back(ZipEntry{NormalizePath(name, name + name_len), U16(e + 10), U32(e + 20), U32(e + 24),
			                        U32(e + 42), static_cast<DWORD>(U16(e + 12) | (static_cast<DWORD>(U16(e + 14)) << 16))});
		p = next;
	}
}

DWORD DataStart(const unsigned char local[30])
{
	if (U32(local) != 0x04034b50)
		return 0;
	return 30 + U16(local + 26) + U16(local + 28);
}

}  // namespace lang
}  // namespace mods
