#pragma once

#ifndef IMAGINE_EXCEPT_H_
#define IMAGINE_EXCEPT_H_

#include <cerrno>
#include <stdexcept>

namespace imagine {
namespace error {

class Exception : private std::runtime_error {
public:
	Exception() : std::runtime_error{ "" }
	{
	}

	using std::runtime_error::runtime_error;

	virtual ~Exception() = default;

	using std::runtime_error::what;
};

#define DECLARE_EXCEPTION(x, base) class x : public base { public: using base::base; };

DECLARE_EXCEPTION(UnknownError, Exception)
DECLARE_EXCEPTION(InternalError, Exception)

DECLARE_EXCEPTION(OutOfMemory, Exception)

DECLARE_EXCEPTION(LogicError, Exception)

DECLARE_EXCEPTION(IllegalArgument, Exception)

DECLARE_EXCEPTION(UnsupportedOperation, Exception)
DECLARE_EXCEPTION(TooManyImagePlanes, Exception)

DECLARE_EXCEPTION(CodecError, Exception)
DECLARE_EXCEPTION(CannotCreateCodec, CodecError)
DECLARE_EXCEPTION(CannotDecodeImage, CodecError)

class IOError : public Exception {
	std::string m_path;
	long long m_off;
	unsigned long long m_count;
	int m_errno;
public:
	IOError(const char *msg, const char *path, long long off = 0, unsigned long long count = 0) :
		Exception{ msg },
		m_off{ off },
		m_count{ count },
		m_errno{ errno }
	{
		try {
			m_path = path;
		} catch (const std::bad_alloc &) {
			// ...
		}
	}

	const std::string &path() const { return m_path; }
	long long off() const { return m_off; }
	unsigned long long count() const { return m_count; }
	int error_code() const { return m_errno; }
};

DECLARE_EXCEPTION(CannotOpenFile, IOError)
DECLARE_EXCEPTION(EndOfFile, IOError)
DECLARE_EXCEPTION(ReadFailed, IOError)
DECLARE_EXCEPTION(WriteFailed, IOError)
DECLARE_EXCEPTION(SeekFailed, IOError)

#undef DECLARE_EXCEPTION

} // namespace error
} // namespace imagine

#endif // IMAGINE_EXCEPT_H_
