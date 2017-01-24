#pragma once

#ifndef IMAGINE_MEMORY_IO_H_
#define IMAGINE_MEMORY_IO_H_

#include <cstddef>
#include <string>
#include "io_context.h"

namespace imagine {

class MemoryIOContext : public IOContext {
	void *m_ptr;
	size_t m_size;
	size_t m_pos;
	std::string m_path;
	bool m_writable;
public:
	MemoryIOContext(const void *ptr, size_t size, const std::string &path);
	MemoryIOContext(void *ptr, size_t size, const std::string &path);

	bool eof() override;

	bool seekable() override;

	const char *path() const override;

	difference_type tell() override;

	size_type size() override;

	difference_type seek_set(difference_type off) override;

	difference_type seek_end(difference_type off) override;

	difference_type seek_rel(difference_type off) override;

	size_type read(void *buf, size_type count) override;

	size_type write(const void *buf, size_type count) override;

	void flush() override;

	void read_all(void *buf, size_type count) override;

	void write_all(const void *buf, size_type count) override;
};

} // namespace imagine

#endif // IMAGINE_MEMORY_IO_H_
