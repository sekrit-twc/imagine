#include <algorithm>
#include <array>
#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <tuple>
#include <utility>
#include <vector>
#include <jpeglib.h>
#include "common/align.h"
#include "common/buffer.h"
#include "common/decoder.h"
#include "common/except.h"
#include "common/format.h"
#include "common/io_context.h"
#include "common/jumpman.h"
#include "common/path.h"
#include "provider/jpeg_decoder.h"

#ifdef IMAGINE_JPEG_ENABLED

namespace imagine {
namespace {

const char JPEG_DECODER_NAME[] = "jpeg";
const std::array<const char *, 8> jpeg_extensions{ { "jpg", "jpeg", "jpe", "jif", "jfif", "jfi" } };

const size_t JPEG_BUFFER_SIZE = 2048;
const JOCTET eoi_marker[] = { 0xFF, JPEG_EOI };

void discard_from_io(IOContext *io, IOContext::size_type count)
{
	char buf[1024];

	while (count) {
		IOContext::size_type n = std::min(count, static_cast<IOContext::size_type>(sizeof(buf)));
		io->read_all(buf, n);
		count -= n;
	}
}

bool recognize_jpeg(IOContext *io)
{
	uint8_t vec[3];
	IOContext::difference_type pos = io->tell();
	bool ret = false;

	io->read_all(vec, sizeof(vec));
	// SOI followed by additional segment.
	ret = vec[0] = 0xFF && vec[1] == 0xD8 && vec[2] == 0xFF;
	io->seek_set(pos);

	return ret;
}

ColorFamily translate_jcs_color(J_COLOR_SPACE color)
{
	switch (color) {
	case JCS_GRAYSCALE:
		return ColorFamily::GRAY;
	case JCS_RGB:
		return ColorFamily::RGB;
	case JCS_YCbCr:
		return ColorFamily::YUV;
	case JCS_CMYK:
		return ColorFamily::CMYK;
	case JCS_YCCK:
		return ColorFamily::YCCK;
	default:
		return ColorFamily::UNKNOWN;
	}
}

class JPEGDecoder : public ImageDecoder {
	jpeg_decompress_struct m_jpeg;
	jpeg_source_mgr m_jpeg_source;
	jpeg_error_mgr m_jpeg_error;

	std::unique_ptr<IOContext> m_io;
	std::vector<JOCTET> m_buffer;
	FileFormat m_format;
	Jumpman m_jumpman;
	bool m_alive;

	static void init_source(j_decompress_ptr) {}

	static boolean fill_input_buffer(j_decompress_ptr cinfo)
	{
		JPEGDecoder *d = static_cast<JPEGDecoder *>(cinfo->client_data);
		bool eof = false;

		try {
			cinfo->src->bytes_in_buffer = d->m_io->read(d->m_buffer.data(), d->m_buffer.size());
			cinfo->src->next_input_byte = d->m_buffer.data();
			if (d->m_io->eof())
				eof = true;
		} catch (...) {
			d->m_jumpman.store_exception();
			eof = true;
		}

		if (eof) {
			cinfo->src->bytes_in_buffer = sizeof(eoi_marker);
			cinfo->src->next_input_byte = eoi_marker;
		}
		return TRUE;
	}

	static void skip_input_data(j_decompress_ptr cinfo, long num_bytes)
	{
		JPEGDecoder *d = static_cast<JPEGDecoder *>(cinfo->client_data);
		bool eof = false;

		try {
			if (static_cast<size_t>(num_bytes) <= cinfo->src->bytes_in_buffer) {
				cinfo->src->bytes_in_buffer -= num_bytes;
				cinfo->src->next_input_byte += num_bytes;
			} else {
				long seek = num_bytes - static_cast<long>(cinfo->src->bytes_in_buffer);

				if (d->m_io->seekable())
					d->m_io->seek_rel(seek);
				else
					discard_from_io(d->m_io.get(), seek);

				cinfo->src->bytes_in_buffer = 0;
				cinfo->src->next_input_byte = d->m_buffer.data();
			}
		} catch (...) {
			d->m_jumpman.store_exception();
			eof = true;
		}

		if (eof) {
			cinfo->src->bytes_in_buffer = sizeof(eoi_marker);
			cinfo->src->next_input_byte = eoi_marker;
		}
	}

	static void term_source(j_decompress_ptr) {}

	static void error_exit(j_common_ptr ptr)
	{
		JPEGDecoder *d = static_cast<JPEGDecoder *>(ptr->client_data);
		d->m_jumpman.execute_jump();
	}

	void decode_header()
	{
		if (!m_alive)
			return;

		if (m_jumpman.call(jpeg_read_header, &m_jpeg, TRUE) != JPEG_HEADER_OK)
			throw error::InternalError{ "jpeglib did not return JPEG_HEADER_OK" };
		if (m_jpeg.num_components == 0)
			throw error::InternalError{ "jpeglib returned 0 planes" };
		if (m_jpeg.num_components > MAX_PLANE_COUNT)
			throw error::TooManyImagePlanes{ "maximum plane count exceeded" };

		m_jumpman.call(jpeg_calc_output_dimensions, &m_jpeg);

		m_format.plane_count = m_jpeg.num_components;
		for (unsigned p = 0; p < m_format.plane_count; ++p) {
			JDIMENSION w = (m_jpeg.image_width * m_jpeg.comp_info[p].h_samp_factor) / m_jpeg.max_h_samp_factor;
			JDIMENSION h = (m_jpeg.image_height * m_jpeg.comp_info[p].v_samp_factor) / m_jpeg.max_v_samp_factor;

			m_format.plane[p].width = (m_jpeg.image_width * m_jpeg.comp_info[p].h_samp_factor) / m_jpeg.max_h_samp_factor;
			m_format.plane[p].height = (m_jpeg.image_height * m_jpeg.comp_info[p].v_samp_factor) / m_jpeg.max_v_samp_factor;
			m_format.plane[p].bit_depth = BITS_IN_JSAMPLE;
		}

		m_format.color_family = translate_jcs_color(m_jpeg.jpeg_color_space);
	}

	void done()
	{
		if (m_alive)
			jpeg_destroy_decompress(&m_jpeg);
		m_alive = false;
	}
public:
	explicit JPEGDecoder(std::unique_ptr<IOContext> io) :
		m_jpeg{},
		m_jpeg_source{},
		m_jpeg_error{},
		m_io{ std::move(io) },
		m_buffer(JPEG_BUFFER_SIZE),
		m_format{ ImageType::JPEG, 1 },
		m_jumpman{ [](void *) { throw error::CannotDecodeImage{ "jpeglib error" }; } , nullptr },
		m_alive{}
	{
		jpeg_std_error(&m_jpeg_error);
		m_jpeg_error.error_exit = &JPEGDecoder::error_exit;
		m_jpeg.err = &m_jpeg_error;

		jpeg_create_decompress(&m_jpeg);
		m_jpeg.client_data = this;
		m_alive = true;

		m_jpeg_source.init_source = &JPEGDecoder::init_source;
		m_jpeg_source.fill_input_buffer = &JPEGDecoder::fill_input_buffer;
		m_jpeg_source.skip_input_data = &JPEGDecoder::skip_input_data;
		m_jpeg_source.resync_to_restart = jpeg_resync_to_restart;
		m_jpeg_source.term_source = &JPEGDecoder::term_source;
		m_jpeg.src = &m_jpeg_source;
	}

	~JPEGDecoder()
	{
		done();
	}

	const char *name() const override
	{
		return JPEG_DECODER_NAME;
	}

	FileFormat file_format() override
	{
		if (!is_constant_format(m_format))
			decode_header();

		return m_format;
	}

	FrameFormat next_frame_format() override
	{
		return m_alive ? file_format() : FrameFormat{};
	}

	void decode(const OutputBuffer &buffer) override try
	{
		if (!m_alive)
			return;

		m_jpeg.raw_data_out = TRUE;
		m_jpeg.output_width = m_jpeg.image_width;
		m_jpeg.output_height = m_jpeg.image_height;
		m_jumpman.call(jpeg_start_decompress, &m_jpeg);

		if (SIZE_MAX / m_jpeg.output_width < m_jpeg.output_height)
			throw error::OutOfMemory{};

		unsigned vstep = DCTSIZE * m_jpeg.max_v_samp_factor;
		JSAMPROW row_index[MAX_PLANE_COUNT][DCTSIZE * MAX_SAMP_FACTOR];
		JSAMPARRAY plane_index[MAX_PLANE_COUNT] = {
			&row_index[0][0], &row_index[1][0], &row_index[2][0], &row_index[3][0],
		};

		std::vector<JSAMPLE> discard_buf;
		for (JDIMENSION i = 0; i < m_jpeg.output_height;) {
			for (unsigned p = 0; p < m_format.plane_count; ++p) {
				JDIMENSION row_offset = (i * m_jpeg.comp_info[p].v_samp_factor) / m_jpeg.max_v_samp_factor;
				unsigned plane_step = (vstep * m_jpeg.comp_info[p].v_samp_factor + m_jpeg.max_v_samp_factor - 1) / m_jpeg.max_v_samp_factor;

				for (unsigned ii = 0; ii < plane_step; ++ii) {
					if (row_offset >= m_format.plane[p].height) {
						discard_buf.resize(m_format.plane[p].width + DCTSIZE * MAX_SAMP_FACTOR);
						row_index[p][ii] = discard_buf.data();
					} else {
						row_index[p][ii] = reinterpret_cast<JSAMPLE *>(static_cast<uint8_t *>(buffer.data[p]) + row_offset * buffer.stride[p]);
					}
					++row_offset;
				}
			}
			i += m_jumpman.call(jpeg_read_raw_data, &m_jpeg, plane_index, vstep);
		}
		m_jumpman.call(jpeg_finish_decompress, &m_jpeg);
		done();
	} catch (const std::bad_alloc &) {
		throw error::OutOfMemory{};
	}
};

} // namespace


const char *JPEGDecoderFactory::name() const
{
	return JPEG_DECODER_NAME;
}

int JPEGDecoderFactory::priority() const
{
	return PRIORITY_HIGH;
}

std::unique_ptr<ImageDecoder> JPEGDecoderFactory::create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> &&io) try
{
	bool recognized;

	if (format)
		recognized = format->type == ImageType::JPEG;
	else if (io->seekable())
		recognized = recognize_jpeg(io.get());
	else
		recognized = is_matching_extension(path, jpeg_extensions.data(), jpeg_extensions.size());

	return recognized ? std::unique_ptr<ImageDecoder>{ new JPEGDecoder{ std::move(io) } } : nullptr;
} catch (const std::bad_alloc &) {
	throw error::OutOfMemory{};
}

} // namespace imagine

#endif // IMAGINE_JPEG_ENABLED
