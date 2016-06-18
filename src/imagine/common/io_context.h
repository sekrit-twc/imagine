#pragma once

#ifndef IMAGINE_IO_CONTEXT_H_
#define IMAGINE_IO_CONTEXT_H_

namespace imagine {

class IOContext {
public:
	typedef unsigned long long size_type;
	typedef long long difference_type;

	virtual ~IOContext() = 0;

	virtual bool eof() = 0;

	virtual bool seekable() = 0;

	virtual const char *path() const = 0;

	virtual difference_type tell() = 0;

	virtual size_type size() = 0;

	virtual difference_type seek_set(difference_type off) = 0;

	virtual difference_type seek_end(difference_type off) = 0;

	virtual difference_type seek_rel(difference_type off) = 0;

	virtual size_type read(void *buf, size_type count) = 0;

	virtual size_type write(const void *buf, size_type count) = 0;

	virtual void flush() = 0;

	virtual void read_all(void *buf, size_type count);

	virtual void write_all(const void *buf, size_type count);
};

} // namespace imagine

#endif // IMAGINE_IO_CONTEXT_H_
