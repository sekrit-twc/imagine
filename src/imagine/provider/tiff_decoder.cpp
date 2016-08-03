#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <utility>
#include <vector>
#include <tiffio.h>
#include "common/buffer.h"
#include "common/except.h"
#include "common/format.h"
#include "common/im_assert.h"
#include "common/io_context.h"
#include "common/path.h"
#include "tiff_decoder.h"

namespace imagine {
namespace {

const char TIFF_DECODER_NAME[] = "tiff";
const std::array<const char *, 2> tiff_extensions{ "tiff", "tif" };

const size_t TIFF_MAGIC_LEN = 4;
const uint8_t tiff_be_magic[4] = { 0x4D, 0x4D, 0x00, 0x2A };
const uint8_t tiff_le_magic[4] = { 0x49, 0x49, 0x2A, 0x00 };

bool recognize_tiff(IOContext *io)
{
	uint8_t vec[TIFF_MAGIC_LEN];
	IOContext::difference_type pos = io->tell();
	bool ret = false;

	io->read_all(vec, TIFF_MAGIC_LEN);
	ret = !memcmp(vec, tiff_be_magic, sizeof(vec)) || !memcmp(vec, tiff_le_magic, sizeof(vec));
	io->seek_set(pos);

	return ret;
}

void depalettize(void * const dst[3], const void *src, unsigned n, const uint16 * const palette[3], unsigned palette_depth)
{
	uint16 *dst_p[3] = { static_cast<uint16 *>(dst[0]), static_cast<uint16 *>(dst[1]), static_cast<uint16 *>(dst[2]) };

	if (palette_depth <= 8) {
		const uint8 *src_p = static_cast<const uint8 *>(src);

		for (unsigned j = 0; j < n; ++j) {
			uint8 x = src_p[j];

			dst_p[0][j] = palette[0][x];
			dst_p[1][j] = palette[1][x];
			dst_p[2][j] = palette[2][x];
		}
	} else {
		const uint16 *src_p = static_cast<const uint16 *>(src);

		for (unsigned j = 0; j < n; ++j) {
			uint16 x = src_p[j];

			dst_p[0][j] = palette[0][x];
			dst_p[1][j] = palette[1][x];
			dst_p[2][j] = palette[2][x];
		}
	}
}

void invert(void *buf, unsigned n, unsigned depth)
{
	unsigned mask = (1UL << depth) - 1;

	if (depth <= 8) {
		uint8 *buf_p = static_cast<uint8 *>(buf);

		for (unsigned j = 0; j < n; ++j) {
			buf_p[j] = ~buf_p[j] & mask;
		}
	} else {
		uint16 *buf_p = static_cast<uint16 *>(buf);

		for (unsigned j = 0; j < n; ++j) {
			buf_p[j] = ~buf_p[j] & mask;
		}
	}
}

void unpack(void * const dst[4], const void *src, unsigned n, unsigned planes, unsigned depth)
{
	if (depth <= 8) {
		uint8 *dst_p[4] = { static_cast<uint8 *>(dst[0]), static_cast<uint8 *>(dst[1]), static_cast<uint8 *>(dst[2]), static_cast<uint8 *>(dst[3]) };
		const uint8 *src_p = static_cast<const uint8 *>(src);

		for (unsigned j = 0; j < n; ++j) {
			for (unsigned p = 0; p < planes; ++p) {
				dst_p[p][j] = src_p[j * planes + p];
			}
		}
	} else {
		uint16 *dst_p[4] = { static_cast<uint16 *>(dst[0]), static_cast<uint16 *>(dst[1]), static_cast<uint16 *>(dst[2]), static_cast<uint16 *>(dst[3]) };
		const uint16 *src_p = static_cast<const uint16 *>(src);

		for (unsigned j = 0; j < n; ++j) {
			for (unsigned p = 0; p < planes; ++p) {
				dst_p[p][j] = src_p[j * planes + p];
			}
		}
	}
}


class TIFFDecoder : public ImageDecoder {
	struct decode_state {
		uint32 image_width;
		uint32 image_height;
		uint16 photometric;
		uint16 samples;
		uint16 bits_per_sample;
		uint16 planar_config;
		uint16 subsample_w;
		uint16 subsample_h;
		uint16 *color_map[3];
	};

	struct tiff_delete {
		void operator()(TIFF *tiff) { TIFFClose(tiff); }
	};

	std::unique_ptr<TIFF, tiff_delete> m_tiff;
	std::exception_ptr m_exception;
	std::unique_ptr<IOContext> m_io;
	FileFormat m_file_format;
	FrameFormat m_frame_format;
	bool m_initial;
	bool m_alive;

	static tsize_t read_proc(thandle_t user, tdata_t buf, tsize_t size)
	{
		TIFFDecoder *d = static_cast<TIFFDecoder *>(user);

		try {
			return d->m_io->read(buf, size);
		} catch (...) {
			d->m_exception = std::current_exception();
			return -1;
		}
	}

	static toff_t seek_proc(thandle_t user, toff_t offset, int origin)
	{
		TIFFDecoder *d = static_cast<TIFFDecoder *>(user);

		try {
			switch (origin) {
			case SEEK_SET:
				return d->m_io->seek_set(offset);
			case SEEK_CUR:
				return d->m_io->seek_rel(offset);
			case SEEK_END:
				return d->m_io->seek_end(offset);
			default:
				return -1;
			}
		} catch (...) {
			d->m_exception = std::current_exception();
			return -1;
		}
	}

	static toff_t size_proc(thandle_t user)
	{
		TIFFDecoder *d = static_cast<TIFFDecoder *>(user);

		try {
			return d->m_io->size();
		} catch (...) {
			d->m_exception = std::current_exception();
			return 0;
		}
	}

	static tsize_t write_proc(thandle_t, tdata_t, tsize_t) { return 0; }

	static int close_proc(thandle_t) { return 0; }

	void current_directory_format(FrameFormat *format)
	{
		TIFF *tiff = m_tiff.get();
		uint32_t w, h;
		uint16 depth;
		uint16 photometric;
		uint16 samplesperpel;

		if (!TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &w))
			throw error::CannotDecodeImage{ "no IMAGEWIDTH tag in TIFF" };
		if (!TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &h))
			throw error::CannotDecodeImage{ "no IMAGEHEIGHT tag in TIFF" };
		if (!TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &depth))
			throw error::CannotDecodeImage{ "no BITSPERSAMPLE tag in TIFF" };
		if (!TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric))
			throw error::CannotDecodeImage{ "no PHOTOMETRIC tag in TIFF" };
		if (!TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samplesperpel))
			throw error::CannotDecodeImage{ "no SAMPLESPERPEL tag in TIFF" };

		if (depth > 16)
			throw error::CannotDecodeImage{ "bit depth too great" };

		switch (photometric) {
		case PHOTOMETRIC_MINISWHITE:
		case PHOTOMETRIC_MINISBLACK:
			format->color_family = ColorFamily::GRAY;
			break;
		case PHOTOMETRIC_RGB:
			format->color_family = ColorFamily::RGB;
			break;
		case PHOTOMETRIC_PALETTE:
			format->color_family = ColorFamily::RGB;
			depth = 16;
			samplesperpel = 3;
			break;
		case PHOTOMETRIC_YCBCR:
			format->color_family = ColorFamily::YUV;
			break;
		default:
			throw error::CannotDecodeImage{ "unknown TIFF photometric intent" };
		}

		uint16 extrasamples;
		uint16 *extrasamples_type;
		if (TIFFGetField(tiff, TIFFTAG_EXTRASAMPLES, &extrasamples, &extrasamples_type) && extrasamples) {
			if (extrasamples > 1)
				throw error::CannotDecodeImage{ "too many extrasamples in TIFF" };

			if (extrasamples_type[0] == EXTRASAMPLE_ASSOCALPHA) {
				switch (format->color_family) {
				case ColorFamily::GRAY:
					format->color_family = ColorFamily::GRAYALPHA;
					break;
				case ColorFamily::RGB:
					format->color_family = ColorFamily::RGBA;
					break;
				case ColorFamily::YUV:
					format->color_family = ColorFamily::YUVA;
					break;
				default:
					throw error::CannotDecodeImage{ "alpha channel not supported in color family" };
				}
			} else {
				throw error::CannotDecodeImage{ "unsupported extrasamples type in TIFF" };
			}
		} else {
			extrasamples = 0;
		}

		switch (photometric) {
		case PHOTOMETRIC_MINISWHITE:
		case PHOTOMETRIC_MINISBLACK:
			if (samplesperpel != 1 + extrasamples)
				throw error::CannotDecodeImage{ "too many samples in greyscale TIFF" };
			break;
		case PHOTOMETRIC_PALETTE:
			if (extrasamples)
				throw error::CannotDecodeImage{ "extrasamples not supported in paletted TIFF" };
			break;
		case PHOTOMETRIC_RGB:
		case PHOTOMETRIC_YCBCR:
			if (samplesperpel != 3 + extrasamples)
				throw error::CannotDecodeImage{ "too many samples in color TIFF" };
			break;
		default:
			break;
		}

		uint16 subsample_w = 1;
		uint16 subsample_h = 1;
		if (photometric == PHOTOMETRIC_YCBCR &&
			!TIFFGetField(tiff, TIFFTAG_YCBCRSUBSAMPLING, &subsample_w, &subsample_h))
		{
			throw error::CannotDecodeImage{ "missing YCBCRSUBSAMPLING tag in YUV TIFF" };
		}

		for (unsigned p = 0; p < samplesperpel; ++p) {
			uint16 sw = (p == 1 || p == 2) ? subsample_w : 1;
			uint16 sh = (p == 1 || p == 2) ? subsample_h : 1;

			format->plane[p].width = w / sw;
			format->plane[p].height = h / sh;
			format->plane[p].bit_depth = depth;
			format->plane[p].floating_point = false;
		}
		format->plane_count = samplesperpel;
	}

	void decode_header()
	{
		if (TIFFLastDirectory(m_tiff.get()))
			current_directory_format(&m_file_format);

		m_initial = false;
	}

	decode_state begin_decode_image()
	{
		TIFF *tiff = m_tiff.get();
		decode_state state{};

		TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &state.image_width);
		TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &state.image_height);
		TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &state.photometric);
		TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &state.samples);
		TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &state.bits_per_sample);

		if (static_cast<size_t>(PTRDIFF_MAX) / (state.image_width * (state.bits_per_sample / 8)) < state.image_height)
			throw error::OutOfMemory{};

		if (state.samples > 1) {
			if (!TIFFGetField(tiff, TIFFTAG_PLANARCONFIG, &state.planar_config))
				throw error::CannotDecodeImage{ "missing PLANARCONFIG tag in tiff" };
		} else {
			state.planar_config = PLANARCONFIG_SEPARATE;
		}

		if (state.photometric == PHOTOMETRIC_PALETTE &&
			!TIFFGetField(tiff, TIFFTAG_COLORMAP, &state.color_map[0], &state.color_map[1], &state.color_map[2]))
		{
			throw error::CannotDecodeImage{ "missing COLORMAP tag in TIFF" };
		}
		if (state.photometric == PHOTOMETRIC_YCBCR)
			TIFFGetField(tiff, TIFFTAG_YCBCRSUBSAMPLING, &state.subsample_w, &state.subsample_h);

		return state;
	}

	void decode_strips(const OutputBuffer &buffer)
	{
		TIFF *tiff = m_tiff.get();
		im_assert_d(!TIFFIsTiled(tiff), "image is tiled");

		decode_state state = begin_decode_image();
		uint32 rows_per_strip;

		if (!TIFFGetField(tiff, TIFFTAG_ROWSPERSTRIP, &rows_per_strip))
			rows_per_strip = state.image_height;

		std::vector<uint8> strip_data(TIFFStripSize(tiff));
		uint32 strip_count = TIFFNumberOfStrips(tiff);
		uint32 strip_num = 0;

		for (unsigned p = 0; p < (state.planar_config == PLANARCONFIG_SEPARATE ? state.samples : 1U); ++p) {
			for (uint32 i = 0; i < state.image_height; i += rows_per_strip) {
				if (TIFFReadEncodedStrip(tiff, strip_num++, strip_data.data(), strip_data.size()) < 0) {
					throw_saved_exception();
					throw error::CannotDecodeImage{ "error decoding TIFF strip" };
				}
				process_tile(state, buffer, strip_data.data(), p, i, 0, state.image_width, rows_per_strip);
			}
		}
	}

	void decode_tiled(const OutputBuffer &buffer)
	{
		TIFF *tiff = m_tiff.get();
		im_assert_d(TIFFIsTiled(tiff), "image not tiled");

		decode_state state = begin_decode_image();
		uint32 tile_width, tile_height;

		TIFFGetField(tiff, TIFFTAG_TILEWIDTH, &tile_width);
		TIFFGetField(tiff, TIFFTAG_TILELENGTH, &tile_height);

		std::vector<uint8> tile_data(TIFFTileSize(tiff));
		uint32 tile_count = TIFFNumberOfTiles(tiff);
		uint32 tile_num = 0;

		for (unsigned p = 0; p < (state.planar_config == PLANARCONFIG_SEPARATE ? state.samples : 1U); ++p) {
			for (uint32 i = 0; i < state.image_height; i += tile_height) {
				for (uint32 j = 0; j < state.image_width; j += tile_width) {
					if (TIFFReadEncodedTile(tiff, tile_num++, tile_data.data(), tile_data.size()) < 0) {
						throw_saved_exception();
						throw error::CannotDecodeImage{ "error decoding TIFF tile" };
					}
					process_tile(state, buffer, tile_data.data(), p, i, j, tile_width, tile_height);
				}
			}
		}
		im_assert_d(tile_num == tile_count, "did not read all tiles in image");
	}

	void process_tile(const decode_state &state, const OutputBuffer &buffer, const void *tile_data,
	                  unsigned p, uint32 i, uint32 j, uint32 tile_width, uint32 tile_height)
	{
		uint32 image_width = state.image_width;
		uint32 image_height = state.image_height;

		if (p == 1 || p == 2) {
			i >>= state.subsample_w;
			j >>= state.subsample_h;
			image_width >>= state.subsample_w;
			image_height >>= state.subsample_h;
			tile_width >>= state.subsample_w;
			tile_height >>= state.subsample_h;
		}

		unsigned bytes_per_sample = state.bits_per_sample / 8;
		ptrdiff_t tile_stride = static_cast<ptrdiff_t>(tile_width) * bytes_per_sample;
		tile_width = std::min(image_width - i, tile_width);
		tile_height = std::min(image_height - i, tile_height);

		if (state.color_map[0]) {
			// Depalettize image.
			void *dst_p[3];

			for (unsigned pp = 0; pp < 3; ++pp) {
				dst_p[pp] = static_cast<uint8 *>(buffer.data[pp]) + i * buffer.stride[pp] + j * bytes_per_sample;
			}
			for (unsigned ti = 0; ti < tile_height; ++ti) {
				depalettize(dst_p, tile_data, tile_width, state.color_map, state.bits_per_sample);

				for (unsigned pp = 0; pp < 3; ++pp) {
					dst_p[pp] = static_cast<uint8 *>(dst_p[pp]) + buffer.stride[pp];
				}
				tile_data = static_cast<const uint8 *>(tile_data) + tile_stride;
			}
		} else if (state.planar_config == PLANARCONFIG_SEPARATE) {
			// Copy tile.
			void *dst_p = static_cast<uint8 *>(buffer.data[p]) + i * buffer.stride[p] + j * bytes_per_sample;

			for (unsigned ti = 0; ti < tile_height; ++ti) {
				std::copy_n(static_cast<const uint8 *>(tile_data), tile_width * bytes_per_sample, static_cast<uint8 *>(dst_p));
				if (state.photometric == PHOTOMETRIC_MINISWHITE)
					invert(dst_p, tile_width, state.bits_per_sample);

				dst_p = static_cast<uint8 *>(dst_p) + buffer.stride[p];
				tile_data = static_cast<const uint8 *>(tile_data) + tile_stride;
			}
		} else if (state.planar_config == PLANARCONFIG_CONTIG) {
			// Packed to planar conversion
			void *dst_p[4];
			tile_stride *= state.samples;

			for (unsigned pp = 0; pp < state.samples; ++pp) {
				dst_p[pp] = static_cast<uint8 *>(buffer.data[pp]) + i * buffer.stride[pp] + j * bytes_per_sample;
			}
			for (unsigned ti = 0; ti < tile_height; ++ti) {
				unpack(dst_p, tile_data, tile_width, state.samples, state.bits_per_sample);

				for (unsigned pp = 0; pp < state.samples; ++pp) {
					dst_p[pp] = static_cast<uint8 *>(dst_p[pp]) + buffer.stride[pp];
				}
				tile_data = static_cast<const uint8 *>(tile_data) + tile_stride;
			}
		} else {
			throw error::CannotDecodeImage{ "unknown image packing" };
		}
	}

	void done()
	{
		m_tiff.reset();
		m_alive = false;
	}

	void throw_saved_exception()
	{
		if (m_exception) {
			std::exception_ptr e;
			std::swap(e, m_exception);
			std::rethrow_exception(e);
		}
	}
public:
	explicit TIFFDecoder(std::unique_ptr<IOContext> &&io) :
		m_tiff{},
		m_io{ std::move(io) },
		m_file_format{ ImageType::TIFF },
		m_initial{},
		m_alive{}
	{
		m_tiff.reset(TIFFClientOpen(
			m_io->path(), "r", this, &TIFFDecoder::read_proc, &TIFFDecoder::write_proc, &TIFFDecoder::seek_proc,
			&TIFFDecoder::close_proc, &TIFFDecoder::size_proc, nullptr, nullptr));
		if (!m_tiff) {
			throw_saved_exception();
			throw error::CannotCreateCodec{ "error creating TIFF context" };
		}

		m_initial = true;
		m_alive = true;
	}

	~TIFFDecoder()
	{
		done();
	}

	const char *name() const override
	{
		return TIFF_DECODER_NAME;
	}

	FileFormat file_format() override
	{
		if (m_initial)
			decode_header();

		return m_file_format;
	}

	FrameFormat next_frame_format() override
	{
		if (!m_alive)
			return{};
		if (is_constant_format(file_format()))
			return file_format();

		if (!is_constant_format(m_frame_format)) {
			if (!TIFFReadDirectory(m_tiff.get())) {
				throw_saved_exception();
				throw error::CannotDecodeImage{ "error reading TIFF directory" };
			}
			current_directory_format(&m_frame_format);
		}
		return m_frame_format;
	}

	void decode(const OutputBuffer &buffer) override try
	{
		if (!m_alive)
			return;

		TIFF *tiff = m_tiff.get();

		// Do decoding.
		if (TIFFIsTiled(tiff))
			decode_tiled(buffer);
		else
			decode_strips(buffer);

		m_frame_format = FrameFormat{};

		if (TIFFLastDirectory(tiff))
			done();
	} catch (const std::bad_alloc &) {
		throw error::OutOfMemory{};
	}
};

} // namespace


const char *TIFFDecoderFactory::name() const
{
	return TIFF_DECODER_NAME;
}

int TIFFDecoderFactory::priority() const
{
	return PRIORITY_NORMAL;
}

std::unique_ptr<ImageDecoder> TIFFDecoderFactory::create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> &&io) try
{
	bool recognized;

	if (format)
		recognized = format->type == ImageType::TIFF;
	else if (io->seekable())
		recognized = recognize_tiff(io.get());
	else
		recognized = is_matching_extension(path, tiff_extensions.data(), tiff_extensions.size());

	return recognized ? std::unique_ptr<ImageDecoder>{ new TIFFDecoder{ std::move(io) } } : nullptr;
} catch (const std::bad_alloc &) {
	throw error::OutOfMemory{};
}

} // namespace imagine
