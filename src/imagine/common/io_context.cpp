#include <cerrno>
#include "except.h"
#include "io_context.h"

namespace imagine {

IOContext::~IOContext() = default;

void IOContext::read_all(void *buf, size_type count)
{
	difference_type where = tell();
	size_type n = 0;

	char *buf_p = static_cast<char *>(buf);

	while (n < count) {
		size_type c = read(buf_p, count - n);
		buf_p += c;
		n += c;

		if (n != count && eof()) {
			errno = 0;
			throw error::EndOfFile{ "eof during read", path(), static_cast<difference_type>(where + n), count - n };
		}
	}
}

void IOContext::write_all(const void *buf, size_type count)
{
	difference_type where = tell();
	size_type n = 0;

	const char *buf_p = static_cast<const char *>(buf);

	while (n < count) {
		size_type c = write(buf_p, count - n);
		buf_p += c;
		n += c;

		if (n != count && eof()) {
			errno = 0;
			throw error::EndOfFile{ "eof during write", path(), static_cast<difference_type>(where + n), count - n };
		}
	}
}

} // namespace imagine
