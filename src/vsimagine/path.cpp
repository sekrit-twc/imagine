#ifdef _WIN32
  #include <codecvt>
  #include <system_error>
  #include <Windows.h>
#endif

#include "path.h"

#ifdef _WIN32
namespace {

std::wstring utf8_to_utf16(const std::string &path)
{
	return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}.from_bytes(path);
}

std::string utf16_to_utf8(const std::wstring &path)
{
	return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}.to_bytes(path);
}

[[noreturn]] void trap_error(const char *msg)
{
	throw std::system_error{ std::error_code{ static_cast<int>(GetLastError()), std::system_category() }, msg };
}

} // namespace


bool path_exists(const std::string &path)
{
	DWORD dwAttrib = GetFileAttributesW(utf8_to_utf16(path).c_str());
	return dwAttrib != INVALID_FILE_ATTRIBUTES;
}

std::string path_canonicalize(const std::string &path)
{
	std::wstring wpath = utf8_to_utf16(path);
	std::wstring out;

	DWORD status = MAX_PATH;
	while (status > out.size()) {
		out.resize(status);
		status = GetFullPathNameW(wpath.c_str(), static_cast<DWORD>(out.size()), &out[0], nullptr);
		if (status == 0)
			trap_error("error canonicalizing path");
	}
	// On success, GetFullPathName returns the number of non-terminating characters.
	out.resize(status);
	return utf16_to_utf8(out);
}
#endif // _WIN32
