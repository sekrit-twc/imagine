#ifndef IMAGINE_H_
#define IMAGINE_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMAGINE_MAKE_API_VERSION(x, y) (((x) << 8) | (y))
#define IMAGINE_API_VERSION_MAJOR 0
#define IMAGINE_API_VERSION_MINOR 0
#define IMAGINE_API_VERSION IMAGINE_MAKE_API_VERSION(IMAGINE_API_VERSION_MAJOR, IMAGINE_API_VERSION_MINOR)

void imagine_get_version(unsigned *major, unsigned *minor, unsigned *micro);
unsigned imagine_get_api_version(unsigned *major, unsigned *minor);

#define IMAGINE_MAX_PLANE_COUNT 4

typedef enum imagine_error_code_e {
	IMAGINE_ERROR_UNKNOWN = -1,
	IMAGINE_ERROR_SUCCESS = 0,

	IMAGINE_ERROR_OUT_OF_MEMORY = 1,

	IMAGINE_ERROR_LOGIC = (1 << 10),

	IMAGINE_ERROR_ILLEGAL_ARGUMENT = (2 << 10),

	IMAGINE_ERROR_UNSUPPORTED_OPERATION = (3 << 10),
	IMAGINE_ERROR_TOO_MANY_IMAGE_PLANES = IMAGINE_ERROR_UNSUPPORTED_OPERATION + 1,

	IMAGINE_ERROR_CODEC               = (4 << 10),
	IMAGINE_ERROR_CANNOT_CREATE_CODEC = IMAGINE_ERROR_CODEC + 1,
	IMAGINE_ERROR_CANNOT_DECODE_IMAGE = IMAGINE_ERROR_CODEC + 2,

	IMAGINE_ERROR_IO               = (5 << 10),
	IMAGINE_ERROR_CANNOT_OPEN_FILE = IMAGINE_ERROR_IO + 1,
	IMAGINE_ERROR_END_OF_FILE      = IMAGINE_ERROR_IO + 2,
	IMAGINE_ERROR_READ_FAILED      = IMAGINE_ERROR_IO + 3,
	IMAGINE_ERROR_WRITE_FAILED     = IMAGINE_ERROR_IO + 4,
	IMAGINE_ERROR_SEEK_FAILED      = IMAGINE_ERROR_IO + 5,
} imagine_error_code_e;

typedef struct imagine_io_error_details {
	const char *path;
	long long off;
	unsigned long long count;
	int errno_;
} imagine_io_error_details;

static imagine_error_code_e imagine_get_error_category(imagine_error_code_e code)
{
	return code < 0 ? code : (imagine_error_code_e)((unsigned)code & 0x7C00);
}

imagine_error_code_e imagine_get_last_error(char *err_msg, size_t n);
void imagine_get_io_error_details(imagine_io_error_details *details);
void imagine_clear_last_error(void);

typedef struct imagine_output_buffer {
	void *data[IMAGINE_MAX_PLANE_COUNT];
	ptrdiff_t stride[IMAGINE_MAX_PLANE_COUNT];
} imagine_output_buffer;


typedef enum imagine_color_family_e {
	IMAGINE_COLOR_FAMILY_UNKNOWN,
	IMAGINE_COLOR_FAMILY_GRAY,
	IMAGINE_COLOR_FAMILY_YUV,
	IMAGINE_COLOR_FAMILY_RGB,
	IMAGINE_COLOR_FAMILY_GRAYALPHA,
	IMAGINE_COLOR_FAMILY_YUVA,
	IMAGINE_COLOR_FAMILY_RGBA,
	IMAGINE_COLOR_FAMILY_YCCK,
	IMAGINE_COLOR_FAMILY_CMYK,
} imagine_color_family_e;

typedef enum imagine_image_type_e {
	IMAGINE_IMAGE_UNKNOWN,
	IMAGINE_IMAGE_BMP,
	IMAGINE_IMAGE_DPX,
	IMAGINE_IMAGE_EXR,
	IMAGINE_IMAGE_JPEG,
	IMAGINE_IMAGE_JPEG2000,
	IMAGINE_IMAGE_PNG,
	IMAGINE_IMAGE_TIFF,
} imagine_image_type_e;

typedef struct imagine_file_format imagine_file_format;

imagine_file_format *imagine_file_format_alloc(void);

void imagine_file_format_free(imagine_file_format *ptr);

void imagine_file_format_clear(imagine_file_format *ptr);

#define IMAGINE_FILE_FORMAT_GET_SET(T, name) \
  T imagine_file_format_##name##_get(const imagine_file_format *ptr); \
  void imagine_file_format_##name##_set(imagine_file_format *ptr, T name)

#define IMAGINE_FILE_FORMAT_GET_SET_P(T, name) \
  T imagine_file_format_##name##_get(const imagine_file_format *ptr, unsigned plane); \
  void imagine_file_format_##name##_set(imagine_file_format *ptr, unsigned plane, T name)

IMAGINE_FILE_FORMAT_GET_SET_P(unsigned, width);
IMAGINE_FILE_FORMAT_GET_SET_P(unsigned, height);
IMAGINE_FILE_FORMAT_GET_SET_P(unsigned, bit_depth);
IMAGINE_FILE_FORMAT_GET_SET_P(int, is_floating_point);
IMAGINE_FILE_FORMAT_GET_SET(unsigned, plane_count);
IMAGINE_FILE_FORMAT_GET_SET(imagine_color_family_e, color_family);
IMAGINE_FILE_FORMAT_GET_SET(imagine_image_type_e, type);
IMAGINE_FILE_FORMAT_GET_SET(unsigned, frame_count);

#undef IMAGINE_FILE_FORMAT_GET_SET
#undef IMAGINE_FILE_FORMAT_GET_SET_P

int imagine_is_constant_format(const imagine_file_format *format);


typedef struct imagine_io_context imagine_io_context;

imagine_io_context *imagine_io_context_from_file_ro(const char *path);

imagine_io_context *imagine_io_context_from_memory(const void *buf, size_t n);

void imagine_io_context_free(imagine_io_context *ptr);


typedef struct imagine_decoder_registry imagine_decoder_registry;
typedef struct imagine_decoder imagine_decoder;

imagine_decoder_registry *imagine_decoder_registry_alloc(void);

void imagine_decoder_registry_free(imagine_decoder_registry *ptr);

void imagine_decoder_registry_disable_provider(imagine_decoder_registry *ptr, const char *name);

imagine_decoder *imagine_decoder_registry_create_decoder(const imagine_decoder_registry *ptr, const char *path, const imagine_file_format *format, imagine_io_context *io);


void imagine_decoder_free(imagine_decoder *ptr);

const char *imagine_decoder_name(const imagine_decoder *ptr);

imagine_error_code_e imagine_decoder_file_format(imagine_decoder *ptr, imagine_file_format *format);

imagine_error_code_e imagine_decoder_next_frame_format(imagine_decoder *ptr, imagine_file_format *format);

imagine_error_code_e imagine_decoder_decode(imagine_decoder *ptr, const imagine_output_buffer *buf);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* IMAGINE_H_ */
