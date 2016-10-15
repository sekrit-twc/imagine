#ifndef IMAGINEPLUSPLUS_HPP_
#define IMAGINEPLUSPLUS_HPP_

#include "imagine.h"

#ifndef IMAGINEXX_NAMESPACE
  #define IMAGINEXX_NAMESPACE imaginexx
#endif

namespace IMAGINEXX_NAMESPACE {

struct im_error {
	imagine_error_code_e code;
	imagine_io_error_details io_details;
	char msg[64];

	im_error() : io_details()
	{
		code = imagine_get_last_error(msg, sizeof(msg));
		if (code >= 0 && imagine_get_error_category(code) == IMAGINE_ERROR_IO)
			imagine_get_io_error_details(&io_details);
	}

	imagine_error_code_e error_category() const
	{
		return imagine_get_error_category(code);
	}
};

struct im_output_buffer : public imagine_output_buffer {
	im_output_buffer() : imagine_output_buffer()
	{
	}
};

class FileFormat {
	imagine_file_format *format;

	FileFormat(const FileFormat &);

	FileFormat &operator=(const FileFormat &);
public:
	explicit FileFormat(imagine_file_format *format) : format(format)
	{
	}

	~FileFormat()
	{
		imagine_file_format_free(format);
	}

	operator imagine_file_format *()
	{
		return format;
	}

	operator const imagine_file_format *() const
	{
		return format;
	}

	void clear()
	{
		imagine_file_format_clear(format);
	}

#define IMAGINEXX_FILE_FORMAT_GET_SET(T, name) \
  T name() const { return imagine_file_format_##name##_get(format); } \
  void set_##name(T val) { imagine_file_format_##name##_set(format, val); }

#define IMAGINEXX_FILE_FORMAT_GET_SET_P(T, name) \
  T name(unsigned plane) const { return imagine_file_format_##name##_get(format, plane); } \
  void set_##name(unsigned plane, T val) { imagine_file_format_##name##_set(format, plane, val); }

	IMAGINEXX_FILE_FORMAT_GET_SET_P(unsigned, width);
	IMAGINEXX_FILE_FORMAT_GET_SET_P(unsigned, height);
	IMAGINEXX_FILE_FORMAT_GET_SET_P(unsigned, bit_depth);

	bool is_floating_point(unsigned plane) const
	{
		return !!imagine_file_format_is_floating_point_get(format, plane);
	}

	void set_is_floating_point(unsigned plane, bool is_floating_point)
	{
		imagine_file_format_is_floating_point_set(format, plane, is_floating_point);
	}

	IMAGINEXX_FILE_FORMAT_GET_SET(unsigned, plane_count);
	IMAGINEXX_FILE_FORMAT_GET_SET(imagine_color_family_e, color_family);
	IMAGINEXX_FILE_FORMAT_GET_SET(imagine_image_type_e, type);
	IMAGINEXX_FILE_FORMAT_GET_SET(unsigned, frame_count);

#undef IMAGINEXX_FILE_FORMAT_GET_SET
#undef IMAGINEXX_FILE_FORMAT_GET_SET_P

	bool is_constant_format() const
	{
		return !!imagine_is_constant_format(format);
	}

	static imagine_file_format *create()
	{
		imagine_file_format *format = imagine_file_format_alloc();

		if (!format)
			throw im_error();

		return format;
	}
};

class IOContext {
	imagine_io_context *io;

	IOContext(const IOContext &);

	IOContext &operator=(const IOContext &);
public:
	explicit IOContext(imagine_io_context *io) : io(io)
	{
	}

	~IOContext()
	{
		imagine_io_context_free(io);
	}

	imagine_io_context *pass()
	{
		imagine_io_context *ret = io;
		io = 0;
		return ret;
	}

	static imagine_io_context *from_file_ro(const char *path)
	{
		imagine_io_context *io;

		if (!(io = imagine_io_context_from_file_ro(path)))
			throw im_error();

		return io;
	}
};

class DecoderRegistry {
	imagine_decoder_registry *registry;

	DecoderRegistry(const DecoderRegistry &);

	DecoderRegistry &operator=(const DecoderRegistry &);
public:
	explicit DecoderRegistry(imagine_decoder_registry *registry) : registry(registry)
	{
	}

	~DecoderRegistry()
	{
		imagine_decoder_registry_free(registry);
	}

	void disable_provider(const char *name)
	{
		imagine_decoder_registry_disable_provider(registry, name);
	}

	imagine_decoder *create_decoder(const char *path, const imagine_file_format *format, imagine_io_context *io)
	{
		imagine_clear_last_error();

		imagine_decoder *decoder = imagine_decoder_registry_create_decoder(registry, path, format, io);
		if (!decoder && imagine_get_last_error(nullptr, 0))
			throw im_error();

		return decoder;
	}

	static imagine_decoder_registry *create()
	{
		imagine_decoder_registry *registry = imagine_decoder_registry_alloc();

		if (!registry)
			throw im_error();

		return registry;
	}
};

class Decoder {
	imagine_decoder *decoder;

	Decoder(const Decoder &);

	Decoder& operator=(const Decoder &);

	void check(int x)
	{
		if (x)
			throw im_error();
	}
public:
	explicit Decoder(imagine_decoder *decoder) : decoder(decoder)
	{
	}

	~Decoder()
	{
		imagine_decoder_free(decoder);
	}

	bool is_null() const
	{
		return decoder == 0;
	}

	const char *name() const
	{
		return imagine_decoder_name(decoder);
	}

	void file_format(imagine_file_format *format)
	{
		check(imagine_decoder_file_format(decoder, format));
	}

	void next_frame_format(imagine_file_format *format)
	{
		check(imagine_decoder_next_frame_format(decoder, format));
	}

	void decode(const imagine_output_buffer &buf)
	{
		check(imagine_decoder_decode(decoder, &buf));
	}
};

} // namespace imaginexx

#endif // IMAGINEPLUSPLUS_HPP_
