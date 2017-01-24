#include <algorithm>
#include <cstdlib>
#include "except.h"
#include "memory_io.h"

namespace imagine {

MemoryIOContext::MemoryIOContext(const void *ptr, size_t size, const std::string &path) :
	m_ptr{ const_cast<void *>(ptr) },
	m_size{ size },
	m_pos{},
	m_path{ path },
	m_writable{}
{
}

MemoryIOContext::MemoryIOContext(void *ptr, size_t size, const std::string &path) :
	m_ptr{ ptr },
	m_size{ size },
	m_pos{},
	m_path{ path },
	m_writable{ true }
{
}

bool MemoryIOContext::eof()
{
	return m_pos == m_size;
}

bool MemoryIOContext::seekable()
{
	return true;
}

const char *MemoryIOContext::path() const
{
	return m_path.c_str();
}

auto MemoryIOContext::tell() -> difference_type
{
	return m_pos;
}

auto MemoryIOContext::size() -> size_type
{
	return m_size;
}

auto MemoryIOContext::seek_set(difference_type off) -> difference_type
{
	if (off < 0 || static_cast<size_type>(off) > m_size)
		throw error::SeekFailed{ "seek out of bounds", path(), off };
	m_pos = static_cast<size_t>(off);
	return m_pos;
}

auto MemoryIOContext::seek_end(difference_type off) -> difference_type
{
	if (off > 0 || static_cast<size_type>(off) < m_size)
		throw error::SeekFailed{ "seek out of bounds", path(), off };
	m_pos = static_cast<size_t>(static_cast<difference_type>(m_size) + off);
	return m_pos;
}

auto MemoryIOContext::seek_rel(difference_type off) -> difference_type
{
	if ((off < 0 && off < -static_cast<difference_type>(m_pos)) || (off > 0 && static_cast<size_type>(off) > m_size - m_pos))
		throw error::SeekFailed{ "seek out of bounds", path(), off };
	m_pos = static_cast<size_t>(static_cast<difference_type>(m_pos) + off);
	return m_pos;
}

auto MemoryIOContext::read(void *buf, size_type count) -> size_type
{
	count = std::min(count, static_cast<size_type>(m_size - m_pos));
	memcpy(buf, static_cast<const char *>(m_ptr) + m_pos, static_cast<size_t>(count));
	m_pos += static_cast<size_t>(count);
	return count;
}

auto MemoryIOContext::write(const void *buf, size_type count) -> size_type
{
	if (!m_writable)
		throw error::WriteFailed{ "buffer not writable", path() };

	count = std::min(count, static_cast<size_type>(m_size - m_pos));
	memcpy(static_cast<char *>(m_ptr) + m_pos, buf, static_cast<size_t>(count));
	m_pos += static_cast<size_t>(count);
	return count;
}

void MemoryIOContext::flush()
{
}

void MemoryIOContext::read_all(void *buf, size_type count)
{
	if (count > m_size - m_pos)
		throw error::EndOfFile{ "insufficient data in buffer", path(), tell(), count };
	read(buf, count);
}

void MemoryIOContext::write_all(const void *buf, size_type count)
{
	if (count > m_size - m_pos)
		throw error::EndOfFile{ "insufficient data in buffer", path(), tell(), count };
	write(buf, count);
}

} // namespace imagine
