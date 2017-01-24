#include <cstring>
#include <exception>
#include <string>
#include <type_traits>
#include "common/buffer.h"
#include "common/decoder.h"
#include "common/except.h"
#include "common/file_io.h"
#include "common/format.h"
#include "common/im_assert.h"
#include "common/io_context.h"
#include "common/memory_io.h"
#include "imagine.h"

namespace {

thread_local imagine_error_code_e g_last_error;
thread_local imagine_io_error_details g_last_io_error;
thread_local std::string g_last_error_msg;
thread_local std::string g_last_io_error_path;

const unsigned VERSION_INFO[] = { 0, 0, 0 };


template <class T, class U>
T *assert_dynamic_type(U *ptr)
{
	im_assert_d(dynamic_cast<T *>(ptr), "bad dynamic type");
	return static_cast<T *>(ptr);
}

void clear_string(std::string *str)
{
	str->clear();
	str->shrink_to_fit();
}

imagine::FileFormat *file_format_cast(imagine_file_format *ptr)
{
	return static_cast<imagine::FileFormat *>(ptr);
}

const imagine::FileFormat *file_format_cast(const imagine_file_format *ptr)
{
	return static_cast<const imagine::FileFormat *>(ptr);
}

imagine::ImageDecoderRegistry *registry_cast(imagine_decoder_registry *ptr)
{
	return static_cast<imagine::ImageDecoderRegistry *>(ptr);
}

const imagine::ImageDecoderRegistry *registry_cast(const imagine_decoder_registry *ptr)
{
	return static_cast<const imagine::ImageDecoderRegistry *>(ptr);
}

void record_exception_message(const imagine::error::Exception &e)
{
	try {
		g_last_error_msg = e.what();
	} catch (const std::bad_alloc &) {
		clear_string(&g_last_error_msg);
	}
}

void record_exception_message(const imagine::error::IOError &e)
{
	record_exception_message(static_cast<const imagine::error::Exception &>(e));

	try {
		g_last_io_error_path = e.path();
	} catch (const std::bad_alloc &) {
		clear_string(&g_last_io_error_path);
	}

	g_last_io_error.off = e.off();
	g_last_io_error.count = e.count();
	g_last_io_error.errno_ = e.error_code();
}

imagine_error_code_e handle_exception(std::exception_ptr eptr)
{
	using namespace imagine::error;

	imagine_error_code_e code = IMAGINE_ERROR_UNKNOWN;

#define CATCH(type, error_code) catch (const type &e) { record_exception_message(e); code = (error_code); }
#define FATAL(type, error_code, msg) catch (const type &e) { im_assert_d(false, msg); record_exception_message(e); code = (error_code); }
	try {
		std::rethrow_exception(eptr);
	}
	CATCH(UnknownError,         IMAGINE_ERROR_UNKNOWN)
	CATCH(OutOfMemory,          IMAGINE_ERROR_OUT_OF_MEMORY)

	CATCH(LogicError,           IMAGINE_ERROR_LOGIC)

	CATCH(IllegalArgument,      IMAGINE_ERROR_ILLEGAL_ARGUMENT)

	CATCH(TooManyImagePlanes,   IMAGINE_ERROR_TOO_MANY_IMAGE_PLANES)
	CATCH(UnsupportedOperation, IMAGINE_ERROR_UNSUPPORTED_OPERATION)

	CATCH(CannotCreateCodec,    IMAGINE_ERROR_CANNOT_CREATE_CODEC)
	CATCH(CannotDecodeImage,    IMAGINE_ERROR_CANNOT_DECODE_IMAGE)
	CATCH(CodecError,           IMAGINE_ERROR_CODEC)

	CATCH(CannotOpenFile,       IMAGINE_ERROR_CANNOT_OPEN_FILE)
	CATCH(EndOfFile,            IMAGINE_ERROR_END_OF_FILE)
	CATCH(ReadFailed,           IMAGINE_ERROR_READ_FAILED)
	CATCH(WriteFailed,          IMAGINE_ERROR_WRITE_FAILED)
	CATCH(SeekFailed,           IMAGINE_ERROR_SEEK_FAILED)
	CATCH(IOError,              IMAGINE_ERROR_IO)

	FATAL(InternalError,        IMAGINE_ERROR_UNKNOWN, "internal error generated")
	FATAL(Exception,            IMAGINE_ERROR_UNKNOWN, "unregistered error generated")
	catch (...) {
		im_assert_d(false, "bad exception type");
		g_last_error_msg[0] = '\0';
	}
#undef CATCH
#undef FATAL
	g_last_error = code;
	return code;
}

void handle_bad_alloc()
{
	imagine_clear_last_error();
	g_last_error = IMAGINE_ERROR_OUT_OF_MEMORY;
}

} // namespace


void imagine_get_version(unsigned *major, unsigned *minor, unsigned *micro)
{
	im_assert_d(major, "null pointer");
	im_assert_d(minor, "null pointer");
	im_assert_d(micro, "null pointer");

	*major = VERSION_INFO[0];
	*minor = VERSION_INFO[1];
	*micro = VERSION_INFO[2];
}

unsigned imagine_get_api_version(unsigned *major, unsigned *minor)
{
	if (major)
		*major = IMAGINE_API_VERSION_MAJOR;
	if (minor)
		*minor = IMAGINE_API_VERSION_MINOR;

	return IMAGINE_API_VERSION;
}

imagine_error_code_e imagine_get_last_error(char *err_msg, size_t n)
{
	if (err_msg && n) {
		std::strncpy(err_msg, g_last_error_msg.c_str(), n);
		err_msg[n - 1] = '\0';
	}

	return g_last_error;
}

void imagine_get_io_error_details(imagine_io_error_details *details)
{
	if (imagine_get_error_category(g_last_error) == IMAGINE_ERROR_IO) {
		*details = g_last_io_error;
		details->path = g_last_io_error_path.c_str();
	}
}

void imagine_clear_last_error(void)
{
	g_last_error = IMAGINE_ERROR_SUCCESS;
	clear_string(&g_last_error_msg);
	clear_string(&g_last_io_error_path);
}

imagine_file_format *imagine_file_format_alloc(void)
{
	try {
		return new imagine::FileFormat{};
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
		return nullptr;
	}
}

void imagine_file_format_free(imagine_file_format *ptr)
{
	delete file_format_cast(ptr);
}

void imagine_file_format_clear(imagine_file_format *ptr)
{
	im_assert_d(ptr, "null pointer");
	*file_format_cast(ptr) = imagine::FileFormat{};
}

unsigned imagine_file_format_width_get(const imagine_file_format *ptr, unsigned plane)
{
	im_assert_d(ptr, "null pointer");
	return file_format_cast(ptr)->plane[plane].width;
}

void imagine_file_format_width_set(imagine_file_format *ptr, unsigned plane, unsigned width)
{
	im_assert_d(ptr, "null pointer");
	file_format_cast(ptr)->plane[plane].width = width;
}

unsigned imagine_file_format_height_get(const imagine_file_format *ptr, unsigned plane)
{
	im_assert_d(ptr, "null pointer");
	return file_format_cast(ptr)->plane[plane].height;
}

void imagine_file_format_height_set(imagine_file_format *ptr, unsigned plane, unsigned height)
{
	im_assert_d(ptr, "null pointer");
	file_format_cast(ptr)->plane[plane].height = height;
}

unsigned imagine_file_format_bit_depth_get(const imagine_file_format *ptr, unsigned plane)
{
	im_assert_d(ptr, "null pointer");
	return file_format_cast(ptr)->plane[plane].bit_depth;
}

void imagine_file_format_bit_depth_set(imagine_file_format *ptr, unsigned plane, unsigned bit_depth)
{
	im_assert_d(ptr, "null pointer");
	file_format_cast(ptr)->plane[plane].bit_depth = bit_depth;
}

int imagine_file_format_is_floating_point_get(const imagine_file_format *ptr, unsigned plane)
{
	im_assert_d(ptr, "null pointer");
	return file_format_cast(ptr)->plane[plane].floating_point;
}

void imagine_file_format_is_floating_point_set(imagine_file_format *ptr, unsigned plane, int is_floating_point)
{
	im_assert_d(ptr, "null pointer");
	file_format_cast(ptr)->plane[plane].floating_point = !!is_floating_point;
}

unsigned imagine_file_format_plane_count_get(const imagine_file_format *ptr)
{
	im_assert_d(ptr, "null pointer");
	return file_format_cast(ptr)->plane_count;
}

void imagine_file_format_plane_count_set(imagine_file_format *ptr, unsigned plane_count)
{
	im_assert_d(ptr, "null pointer");
	file_format_cast(ptr)->plane_count = plane_count;
}

imagine_color_family_e imagine_file_format_color_family_get(const imagine_file_format *ptr)
{
	im_assert_d(ptr, "null pointer");

	switch (file_format_cast(ptr)->color_family) {
	case imagine::ColorFamily::GRAY:
		return IMAGINE_COLOR_FAMILY_GRAY;
	case imagine::ColorFamily::YUV:
		return IMAGINE_COLOR_FAMILY_YUV;
	case imagine::ColorFamily::RGB:
		return IMAGINE_COLOR_FAMILY_RGB;
	case imagine::ColorFamily::GRAYALPHA:
		return IMAGINE_COLOR_FAMILY_GRAYALPHA;
	case imagine::ColorFamily::YUVA:
		return IMAGINE_COLOR_FAMILY_YUVA;
	case imagine::ColorFamily::RGBA:
		return IMAGINE_COLOR_FAMILY_RGBA;
	case imagine::ColorFamily::YCCK:
		return IMAGINE_COLOR_FAMILY_YCCK;
	case imagine::ColorFamily::CMYK:
		return IMAGINE_COLOR_FAMILY_CMYK;
	default:
		return IMAGINE_COLOR_FAMILY_UNKNOWN;
	}
}

void imagine_file_format_color_family_set(imagine_file_format *ptr, imagine_color_family_e color_family)
{
	im_assert_d(ptr, "null pointer");
	imagine::FileFormat *format = file_format_cast(ptr);

	switch (color_family) {
	case IMAGINE_COLOR_FAMILY_GRAY:
		format->color_family = imagine::ColorFamily::GRAY;
		break;
	case IMAGINE_COLOR_FAMILY_YUV:
		format->color_family = imagine::ColorFamily::YUV;
		break;
	case IMAGINE_COLOR_FAMILY_RGB:
		format->color_family = imagine::ColorFamily::RGB;
		break;
	case IMAGINE_COLOR_FAMILY_GRAYALPHA:
		format->color_family = imagine::ColorFamily::GRAYALPHA;
		break;
	case IMAGINE_COLOR_FAMILY_YUVA:
		format->color_family = imagine::ColorFamily::YUVA;
		break;
	case IMAGINE_COLOR_FAMILY_RGBA:
		format->color_family = imagine::ColorFamily::RGBA;
		break;
	case IMAGINE_COLOR_FAMILY_YCCK:
		format->color_family = imagine::ColorFamily::YCCK;
		break;
	case IMAGINE_COLOR_FAMILY_CMYK:
		format->color_family = imagine::ColorFamily::CMYK;
		break;
	default:
		format->color_family = imagine::ColorFamily::UNKNOWN;
		break;
	}
}

imagine_image_type_e imagine_file_format_type_get(const imagine_file_format *ptr)
{
	im_assert_d(ptr, "null pointer");

	switch (file_format_cast(ptr)->type) {
	case imagine::ImageType::BMP:
		return IMAGINE_IMAGE_BMP;
	case imagine::ImageType::DPX:
		return IMAGINE_IMAGE_DPX;
	case imagine::ImageType::EXR:
		return IMAGINE_IMAGE_EXR;
	case imagine::ImageType::JPEG:
		return IMAGINE_IMAGE_JPEG;
	case imagine::ImageType::JPEG2000:
		return IMAGINE_IMAGE_JPEG2000;
	case imagine::ImageType::PNG:
		return IMAGINE_IMAGE_PNG;
	case imagine::ImageType::TIFF:
		return IMAGINE_IMAGE_TIFF;
	default:
		return IMAGINE_IMAGE_UNKNOWN;
	}
}

void imagine_file_format_type_set(imagine_file_format *ptr, imagine_image_type_e type)
{
	im_assert_d(ptr, "null pointer");
	imagine::FileFormat *format = file_format_cast(ptr);

	switch (type) {
	case IMAGINE_IMAGE_BMP:
		format->type = imagine::ImageType::BMP;
		break;
	case IMAGINE_IMAGE_DPX:
		format->type = imagine::ImageType::DPX;
		break;
	case IMAGINE_IMAGE_EXR:
		format->type = imagine::ImageType::EXR;
		break;
	case IMAGINE_IMAGE_JPEG:
		format->type = imagine::ImageType::JPEG;
		break;
	case IMAGINE_IMAGE_JPEG2000:
		format->type = imagine::ImageType::JPEG2000;
		break;
	case IMAGINE_IMAGE_PNG:
		format->type = imagine::ImageType::PNG;
		break;
	case IMAGINE_IMAGE_TIFF:
		format->type = imagine::ImageType::TIFF;
		break;
	default:
		format->type = imagine::ImageType::UNKNOWN;
		break;
	}
}

int imagine_is_constant_format(const imagine_file_format *format)
{
	im_assert_d(format, "null pointer");
	return imagine::is_constant_format(*file_format_cast(format));
}

imagine_io_context *imagine_io_context_from_file_ro(const char *path)
{
	try {
		return new imagine::FileIOContext{ path, imagine::FileIOContext::read_tag };
	} catch (const imagine::error::Exception &) {
		handle_exception(std::current_exception());
		return nullptr;
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
		return nullptr;
	}
}

imagine_io_context *imagine_io_context_from_memory(const void *buf, size_t n, const char *path)
{
	im_assert_d(buf && n, "null pointer");

	try {
		return new imagine::MemoryIOContext{ buf, n, path ? path : "" };
	} catch (const imagine::error::Exception &) {
		handle_exception(std::current_exception());
		return nullptr;
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
		return nullptr;
	}
}

void imagine_io_context_free(imagine_io_context *ptr)
{
	delete ptr;
}

imagine_decoder_registry *imagine_decoder_registry_alloc(void)
{
	try {
		std::unique_ptr<imagine::ImageDecoderRegistry> registry{ new imagine::ImageDecoderRegistry{} };
		registry->register_default_providers();
		return registry.release();
	} catch (const imagine::error::Exception &) {
		handle_exception(std::current_exception());
		return nullptr;
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
		return nullptr;
	}
}

void imagine_decoder_registry_free(imagine_decoder_registry *ptr)
{
	delete static_cast<imagine::ImageDecoderRegistry *>(ptr);
}

void imagine_decoder_registry_disable_provider(imagine_decoder_registry *ptr, const char *name)
{
	im_assert_d(ptr, "null pointer");
	im_assert_d(name, "null pointer");

	registry_cast(ptr)->disable_provider(name);
}

imagine_decoder *imagine_decoder_registry_create_decoder(const imagine_decoder_registry *ptr, const char *path, const imagine_file_format *format, imagine_io_context *io)
{
	im_assert_d(ptr, "null pointer");
	im_assert_d(io, "null pointer");

	std::unique_ptr<imagine::IOContext> io_uptr{ assert_dynamic_type<imagine::IOContext>(io) };

	try {
		return registry_cast(ptr)->create_decoder(path, file_format_cast(format), std::move(io_uptr)).release();
	} catch (const imagine::error::Exception &) {
		handle_exception(std::current_exception());
		return nullptr;
	}
}

void imagine_decoder_free(imagine_decoder *ptr)
{
	delete ptr;
}

const char *imagine_decoder_name(const imagine_decoder *ptr)
{
	im_assert_d(ptr, "null pointer");
	return assert_dynamic_type<const imagine::ImageDecoder>(ptr)->name();
}

#define EX_BEGIN \
  imagine_error_code_e ret = IMAGINE_ERROR_SUCCESS; \
  try {
#define EX_END \
  } catch (...) { \
    ret = handle_exception(std::current_exception()); \
  } \
  return ret;

imagine_error_code_e imagine_decoder_file_format(imagine_decoder *ptr, imagine_file_format *format)
{
	im_assert_d(ptr, "null pointer");
	im_assert_d(format, "null pointer");

	EX_BEGIN
	*file_format_cast(format) = assert_dynamic_type<imagine::ImageDecoder>(ptr)->file_format();
	EX_END
}

imagine_error_code_e imagine_decoder_next_frame_format(imagine_decoder *ptr, imagine_file_format *format)
{
	im_assert_d(ptr, "null pointer");
	im_assert_d(format, "null pointer");

	EX_BEGIN
	imagine_file_format_clear(format);
	*static_cast<imagine::FrameFormat *>(file_format_cast(format)) = assert_dynamic_type<imagine::ImageDecoder>(ptr)->next_frame_format();
	EX_END
}

imagine_error_code_e imagine_decoder_decode(imagine_decoder *ptr, const imagine_output_buffer *buf)
{
	static_assert(imagine::MAX_PLANE_COUNT == IMAGINE_MAX_PLANE_COUNT, "plane counts mismatch");

	im_assert_d(ptr, "null pointer");
	im_assert_d(buf, "null pointer");

	EX_BEGIN
	imagine::OutputBuffer buffer;
	for (unsigned p = 0; p < imagine::MAX_PLANE_COUNT; ++p) {
		buffer.data[p] = buf->data[p];
		buffer.stride[p] = buf->stride[p];
	}
	assert_dynamic_type<imagine::ImageDecoder>(ptr)->decode(buffer);
	EX_END
}

#undef EX_BEGIN
#undef EX_END
