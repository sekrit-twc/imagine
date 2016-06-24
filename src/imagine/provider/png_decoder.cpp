#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>
#include <libp2p/p2p.h>
#include <png.h>
#include "common/buffer.h"
#include "common/decoder.h"
#include "common/except.h"
#include "common/format.h"
#include "common/im_assert.h"
#include "common/io_context.h"
#include "common/jumpman.h"
#include "png_decoder.h"

#define IMAGINE_PNG_ENABLED
#ifdef IMAGINE_PNG_ENABLED

namespace imagine {
namespace {

const char PNG_DECODER_NAME[] = "png";
const char PNG_EXTENSIONS[][4] = { "png" };
const uint8_t PNG_MAGIC[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };

using packed_ay8 = p2p::byte_packed_444_be<uint8_t, uint16_t, p2p::make_mask(p2p::C__, p2p::C__, p2p::C_A, p2p::C_Y)>;
using packed_ay16 = p2p::byte_packed_444_be<uint16_t, uint32_t, p2p::make_mask(p2p::C__, p2p::C__, p2p::C_A, p2p::C_Y)>;

typedef void(*unpack_func)(const void *, void * const *, unsigned, unsigned);

bool is_png_extension(const char *path)
{
	const char *ptr = strrchr(path, '.');
	if (!ptr)
		return false;

	for (const char *ext : PNG_EXTENSIONS) {
		if (!strcmp(ptr, ext))
			return true;
	}
	return false;
}

bool recognize_png(IOContext *io)
{
	uint8_t vec[sizeof(PNG_MAGIC)];
	IOContext::difference_type pos = io->tell();
	bool ret = false;

	io->read_all(vec, sizeof(vec));
	if (!png_sig_cmp(vec, 0, sizeof(vec)))
		ret = true;

	io->seek_set(pos);
	return ret;
}

ColorFamily translate_png_color(png_byte color_type, unsigned plane_count)
{
	_im_assert_d(!(color_type & PNG_COLOR_MASK_PALETTE), "palette must be removed");

	switch (color_type) {
	case PNG_COLOR_TYPE_GRAY:
		_im_assert_d(plane_count == 1, "");
		return ColorFamily::GRAY;
	case PNG_COLOR_TYPE_RGB:
		_im_assert_d(plane_count == 3, "");
		return ColorFamily::RGB;
	case PNG_COLOR_TYPE_RGB_ALPHA:
		_im_assert_d(plane_count == 4, "");
		return ColorFamily::RGBA;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		_im_assert_d(plane_count == 2, "");
		return ColorFamily::GRAYALPHA;
	default:
		throw error::CannotDecodeImage{ "unknown color_type" };
	}
}

unpack_func select_unpack(const FrameFormat &format)
{
	bool high_depth = format.plane[0].bit_depth > 8;

	// libPNG always returns big-endian samples.
	switch (format.color_family) {
	case ColorFamily::GRAY:
		return nullptr;
	case ColorFamily::RGB:
		return high_depth ? p2p::packed_to_planar<p2p::packed_rgb48_be>::unpack : p2p::packed_to_planar<p2p::packed_rgb24_be>::unpack;
	case ColorFamily::GRAYALPHA:
		return high_depth ? p2p::packed_to_planar<packed_ay16>::unpack : p2p::packed_to_planar<packed_ay8>::unpack;
	case ColorFamily::RGBA:
		return high_depth ? p2p::packed_to_planar<p2p::packed_argb64_be>::unpack : p2p::packed_to_planar<p2p::packed_argb32_be>::unpack;
	default:
		throw error::CannotDecodeImage{ "unsupported color_type" };
	}
}

class PNGDecoder : public ImageDecoder {
	png_structp m_png;
	png_infop m_png_info;
	unsigned m_png_passes;

	std::unique_ptr<IOContext> m_io;
	FileFormat m_format;
	Jumpman m_jumpman;
	bool m_alive;

	static void error_fn(png_structp p, png_const_charp error_msg)
	{
		PNGDecoder *d = static_cast<PNGDecoder *>(png_get_io_ptr(p));
		d->m_jumpman.execute_jump();
	}

	static void read_fn(png_structp p, png_bytep buf, png_size_t count)
	{
		PNGDecoder *d = static_cast<PNGDecoder *>(png_get_io_ptr(p));

		try {
			d->m_io->read_all(buf, count);
		} catch (...) {
			d->m_jumpman.store_exception();
			png_error(p, "exception");
		}
	}

	void decode_header()
	{
		if (!m_alive)
			return;

		m_jumpman.call(png_read_info, m_png, m_png_info);

		if (png_get_color_type(m_png, m_png_info) & PNG_COLOR_MASK_PALETTE)
			png_set_palette_to_rgb(m_png);
		if (png_get_valid(m_png, m_png_info, PNG_INFO_tRNS))
			png_set_tRNS_to_alpha(m_png);

		if (png_get_interlace_type(m_png, m_png_info))
			m_png_passes = png_set_interlace_handling(m_png);
		else
			m_png_passes = 1;

		// Disable gamma processing.
		png_set_gamma(m_png, 1.0, 1.0);
		// Get rid of bit-packed formats.
		png_set_expand_gray_1_2_4_to_8(m_png);

		m_jumpman.call(png_read_update_info, m_png, m_png_info);

		unsigned w = png_get_image_width(m_png, m_png_info);
		unsigned h = png_get_image_height(m_png, m_png_info);
		unsigned depth = png_get_bit_depth(m_png, m_png_info);
		unsigned color_type = png_get_color_type(m_png, m_png_info);

		// Swap R-G-B-A to A-R-G-B so that p2p can unpack it.
		png_set_swap_alpha(m_png);

		m_format.plane_count = png_get_channels(m_png, m_png_info);
		for (unsigned p = 0; p < m_format.plane_count; ++p) {
			m_format.plane[p].width = w;
			m_format.plane[p].height = h;
			m_format.plane[p].bit_depth = depth;
		}

		m_format.color_family = translate_png_color(color_type, m_format.plane_count);
	}

	void decode_one_pass(const OutputBuffer &buffer) try
	{
		png_size_t rowsize = png_get_rowbytes(m_png, m_png_info);
		std::vector<uint8_t> row(rowsize);

		void *dst_p[MAX_PLANE_COUNT] = {};
		for (unsigned p = 0; p < m_format.plane_count; ++p) {
			dst_p[p] = buffer.data[p];
		}

		unpack_func unpack = select_unpack(m_format);

		for (unsigned i = 0; i < m_format.plane[0].height; ++i) {
			m_jumpman.call(png_read_row, m_png, unpack ? row.data() : static_cast<uint8_t *>(dst_p[0]), nullptr);

			if (m_format.color_family == ColorFamily::GRAYALPHA)
				dst_p[3] = dst_p[1];
			if (unpack)
				unpack(row.data(), dst_p, 0, m_format.plane[0].width);

			for (unsigned p = 0; p < m_format.plane_count; ++p) {
				dst_p[p] = static_cast<uint8_t *>(dst_p[p]) + buffer.stride[p];
			}
		}
	} catch (const std::bad_alloc &) {
		throw error::OutOfMemory{};
	}

	void decode_interlaced(const OutputBuffer &buffer) try
	{
		png_size_t rowsize = png_get_rowbytes(m_png, m_png_info);

		if (SIZE_MAX / rowsize < m_format.plane[0].height)
			throw error::OutOfMemory{};

		std::vector<uint8_t> image(rowsize * m_format.plane[0].height);
		std::vector<uint8_t *> row_index(m_format.plane[0].height);

		for (unsigned i = 0; i < m_format.plane[0].height; ++i) {
			row_index[i] = image.data() + i * rowsize;
		}
		for (unsigned pass = 0; pass < m_png_passes; ++pass) {
			m_jumpman.call(png_read_image, m_png, row_index.data());
		}

		void *dst_p[MAX_PLANE_COUNT] = {};
		for (unsigned p = 0; p < m_format.plane_count; ++p) {
			dst_p[p] = buffer.data[p];
		}

		unpack_func unpack = select_unpack(m_format);

		for (unsigned i = 0; i < m_format.plane[0].height; ++i) {
			if (m_format.color_family == ColorFamily::GRAYALPHA)
				dst_p[3] = dst_p[1];
			if (unpack)
				unpack(image.data() + i * rowsize, dst_p, 0, m_format.plane[0].width);

			for (unsigned p = 0; p < m_format.plane_count; ++p) {
				dst_p[p] = static_cast<uint8_t *>(dst_p[p]) + buffer.stride[p];
			}
		}
	} catch (const std::bad_alloc &) {
		throw error::OutOfMemory{};
	}

	void done()
	{
		png_destroy_read_struct(&m_png, &m_png_info, nullptr);
		m_alive = false;
	}
public:
	explicit PNGDecoder(std::unique_ptr<IOContext> io) :
		m_png{},
		m_png_info{},
		m_png_passes{},
		m_io{ std::move(io) },
		m_format{ ImageType::PNG, 1 },
		m_jumpman{ [](void *) { throw error::CannotDecodeImage{ "pnglib error" }; }, nullptr },
		m_alive{}
	{
		try {
			m_png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
			m_png_info = png_create_info_struct(m_png);
			png_set_read_fn(m_png, this, &PNGDecoder::read_fn);
			png_set_error_fn(m_png, this, &PNGDecoder::error_fn, nullptr);
		} catch (...) {
			png_destroy_read_struct(&m_png, &m_png_info, nullptr);
			throw;
		}

		m_alive = true;
	}

	~PNGDecoder()
	{
		done();
	}

	const char *name() const override
	{
		return PNG_DECODER_NAME;
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

	void decode(const OutputBuffer &buffer) override
	{
		if (!m_alive)
			return;

		if (m_png_passes == 1)
			decode_one_pass(buffer);
		else
			decode_interlaced(buffer);

		m_jumpman.call(png_read_end, m_png, nullptr);
		done();
	}
};

} // namespace


const char *PNGDecoderFactory::name() const
{
	return PNG_DECODER_NAME;
}

int PNGDecoderFactory::priority() const
{
	return PRIORITY_HIGH;
}

std::unique_ptr<ImageDecoder> PNGDecoderFactory::create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> &&io)
{
	bool recognized;

	if (format)
		recognized = format->type == ImageType::PNG;
	else if (io->seekable())
		recognized = recognize_png(io.get());
	else
		recognized = is_png_extension(path);

	return recognized ? std::unique_ptr<ImageDecoder>{ new PNGDecoder{ std::move(io) } } : nullptr;
}

} // namespace imagine

#endif // IMAGINE_PNG_ENABLED
