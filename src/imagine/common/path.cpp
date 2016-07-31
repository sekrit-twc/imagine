#include <cstring>
#include <locale>
#include "path.h"

namespace imagine {

namespace {

bool eq_case_insensitive(const char *a, const char *b, const std::locale &loc)
{
	while (*a != '\0' && *b != '\0') {
		char x = std::tolower(*a++, loc);
		char y = std::tolower(*b++, loc);
		if (x != y)
			return false;
	}
	return *a == '\0' && *b == '\0';
}

} // namespace

bool is_matching_extension(const char *path, const char * const *extensions, size_t num_extensions)
{
	const char *ptr = std::strrchr(path, '.');
	if (!ptr)
		return false;

	const std::locale &loc_classic = std::locale::classic();

	for (size_t i = 0; i < num_extensions; ++i) {
		if (eq_case_insensitive(ptr, extensions[i], loc_classic))
			return true;
	}
	return false;
}

} // namespace imagine
