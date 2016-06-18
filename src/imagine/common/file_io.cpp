#ifndef _WIN32
  #define _FILE_OFFSET_BITS 64
#endif // _WIN32

#include <cstdio>
#include <utility>
#include <sys/stat.h>
#include <sys/types.h>
#include "except.h"
#include "file_io.h"

#ifdef _WIN32
  #include <io.h>
  #define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
  #define fileno _fileno
  #define ftello _ftelli64
  #define fseeko _fseeki64
  #define fstat64 _fstat64
  #define isatty _isatty
  #define struct_stat64 __stat64
#else
  #include <unistd.h>
  #define struct_stat64 stat64
#endif

namespace imagine {
namespace {

std::FILE *file_cast(void *file)
{
	return static_cast<std::FILE *>(file);
}

std::FILE *file_cast(const FileIOHandle &file)
{
	return file_cast(file.get());
}

FileIOHandle open_file(const char *path, const char *mode)
{
	FileIOHandle handle{ std::fopen(path, mode) };
	if (!handle)
		throw error::CannotOpenFile{ "error opening file", path };
	return handle;
}

bool is_seekable(std::FILE *file)
{
	int fd = fileno(file);
	struct struct_stat64 st;

	if (isatty(fd))
		return false;
	if (fstat64(fd, &st))
		return false;

	return S_ISREG(st.st_mode);
}

} // namespace


void FileIOHandle::close_file::operator()(void *file)
{
	if (file && should_close)
		std::fclose(file_cast(file));
}

FileIOContext::FileIOContext(FileIOHandle file, const std::string &path) :
	m_file{ std::move(file) },
	m_path{ path },
	m_offset{},
	m_seekable{ is_seekable(file_cast(m_file)) }
{
}

FileIOContext::FileIOContext(const std::string &path, read_tag_type) :
	FileIOContext{ open_file(path.c_str(), "rb"), std::move(path) }
{
}

FileIOContext::FileIOContext(const std::string &path, write_tag_type) :
	FileIOContext{ open_file(path.c_str(), "wb"), std::move(path) }
{
}

FileIOContext::FileIOContext(const std::string &path, append_tag_type) :
	FileIOContext{ open_file(path.c_str(), "wb+"), std::move(path) }
{
}

FileIOContext::FileIOContext(const std::string &path, rw_tag_type) :
	FileIOContext{ open_file(path.c_str(), "rb+"), std::move(path) }
{
}

FileIOContext::~FileIOContext() = default;

void FileIOContext::check_seekable()
{
	if (!m_seekable) {
		errno = 0;
		throw error::SeekFailed{ "file not seekable", path() };
	}
}

auto FileIOContext::update_file_pointer() -> difference_type
{
	check_seekable();

	ptrdiff_t where = std::ftell(file_cast(m_file));
	if (where < 0)
		throw error::SeekFailed{ "error determinig file position", path() };
	m_offset = where;
	return where;
}

bool FileIOContext::eof()
{
	return !!std::feof(file_cast(m_file));
}

bool FileIOContext::seekable()
{
	return m_seekable;
}

const char *FileIOContext::path() const
{
	return m_path.c_str();
}

auto FileIOContext::tell() -> difference_type
{
	return m_offset;
}

auto FileIOContext::size() -> size_type
{
	check_seekable();

	int fd = fileno(file_cast(m_file));
	struct struct_stat64 st;

	if (fstat64(fd, &st))
		throw error::SeekFailed{ "unable to determine file size", path() };
	return st.st_size;
}

auto FileIOContext::seek_set(difference_type off) -> difference_type
{
	check_seekable();

	if (fseeko(file_cast(m_file), off, SEEK_SET))
		throw error::SeekFailed{ "error seeking (from begin)", path(), off };
	return update_file_pointer();
}

auto FileIOContext::seek_end(difference_type off) -> difference_type
{
	check_seekable();

	if (fseeko(file_cast(m_file), off, SEEK_END))
		throw error::SeekFailed{ "error seeking (from end)", path(), off };
	return update_file_pointer();
}

auto FileIOContext::seek_rel(difference_type off) -> difference_type
{
	check_seekable();

	difference_type where = tell();
	if (fseeko(file_cast(m_file), off, SEEK_CUR))
		throw error::SeekFailed{ "error seeking", path(), where + off };
	return update_file_pointer();
}

auto FileIOContext::read(void *buf, size_type count) -> size_type
{
	std::FILE *file = file_cast(m_file);
	size_t n = std::fread(buf, 1, count, file);

	m_offset += n;
	if (n != count && std::ferror(file))
		throw error::ReadFailed{ "error reading", path(),  m_offset, count - n };
	return n;
}

auto FileIOContext::write(const void *buf, size_type count) -> size_type
{
	std::FILE *file = file_cast(m_file);
	size_t n = std::fwrite(buf, 1, count, file);

	m_offset += n;
	if (n != count && std::ferror(file))
		throw error::WriteFailed{ "error writing", path(), m_offset, count - n };
	return n;
}

void FileIOContext::flush()
{
	if (std::fflush(file_cast(m_file)) < 0)
		throw error::WriteFailed{ "error flushing", path() };
}

const FileIOContext::read_tag_type FileIOContext::read_tag;
const FileIOContext::write_tag_type FileIOContext::write_tag;
const FileIOContext::append_tag_type FileIOContext::append_tag;
const FileIOContext::rw_tag_type FileIOContext::rw_tag;

} // namespace imagine
