#pragma once

#ifndef IMAGINE_FILE_IO_H_
#define IMAGINE_FILE_IO_H_

#include <memory>
#include <string>
#include "io_context.h"

namespace imagine {

class FileIOHandle {
	struct close_file {
		void operator()(void *file);
		bool should_close;
	};

	std::unique_ptr<void, close_file> m_file;
public:
	FileIOHandle() = default;

	explicit FileIOHandle(void *stdio_file, bool should_close = true) :
		m_file{ stdio_file, { should_close } }
	{
	}

	void *get() const { return m_file.get(); };
	void *release() { return m_file.release(); }

	explicit operator bool() { return !!m_file; }
};

class FileIOContext : public IOContext {
	struct read_tag_type {};
	struct write_tag_type {};
	struct append_tag_type {};
	struct rw_tag_type {};
public:
	static const read_tag_type read_tag;
	static const write_tag_type write_tag;
	static const append_tag_type append_tag;
	static const rw_tag_type rw_tag;
protected:
	FileIOHandle m_file;
	std::string m_path;
	difference_type m_offset;
	bool m_seekable;

	void check_seekable();
	difference_type update_file_pointer();
public:
	FileIOContext(FileIOHandle file, const std::string &path);

	explicit FileIOContext(const std::string &path, read_tag_type = read_tag);
	FileIOContext(const std::string &path, write_tag_type);
	FileIOContext(const std::string &path, append_tag_type);
	FileIOContext(const std::string &path, rw_tag_type);

	~FileIOContext();

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
};

} // namespace imagine

#endif // IMAGINE_FILE_IO_H_
