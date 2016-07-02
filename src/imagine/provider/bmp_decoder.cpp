#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>
#include "libp2p/p2p.h"
#include "common/align.h"
#include "common/buffer.h"
#include "common/decoder.h"
#include "common/except.h"
#include "common/format.h"
#include "common/io_context.h"
#include "common/im_assert.h"
#include "bmp_decoder.h"

#ifdef _MSC_VER
  #pragma warning(disable : 4146)
#endif

#ifdef _WIN32
  #define LITTLE_ENDIAN
#else
  #include <endian.h>
  #if __BYTE_ORDER == __BIG_ENDIAN
    #define BIG_ENDIAN
  #elif __BYTE_ORDER == __LITTLE_ENDIAN
    #define LITTLE_ENDIAN
  #else
    #error bad endian
  #endif
#endif // _WIN32

namespace imagine {
namespace {

const char BMP_DECODER_NAME[] = "bmp";
const char BMP_EXTENSIONS[][4] = { "bmp", "dib" };

const size_t BITMAPCOREHEADER_SIZE = 12;
const size_t OS22XBITMAPHEADER_SIZE = 64;
const size_t BITMAPINFOHEADER_SIZE = 40;
const size_t BITMAPV2INFOHEADER_SIZE = 52;
const size_t BITMAPV3INFOHEADER_SIZE = 56;
const size_t BITMAPV4HEADER_SIZE = 108;
const size_t BITMAPV5HEADER_SIZE = 124;

const uint16_t BITMAP_MAGIC = ('M' << 8) | 'B';

#ifdef BIG_ENDIAN
uint16_t to_le(uint16_t x) { return __builtin_bswap16(x); }
int32_t to_le(int32_t x) { return __builtin_bswap32(x); }
uint32_t to_le(uint32_t x) { return __builtin_bswap32(x); }
#else
template <class T>
T to_le(T x) { return x; }
#endif // BIG_ENDIAN

template <class T>
unsigned bsf(T x)
{
	for (unsigned i = 0; i < std::numeric_limits<T>::digits; ++i) {
		if (x & (static_cast<T>(1) << i))
			return i;
	}
	return 0;
}

template <class T>
unsigned bsr(T x)
{
	for (unsigned i = std::numeric_limits<T>::digits; i != 0; --i) {
		if (x & (static_cast<T>(1) << (i - 1)))
			return i;
	}
	return 0;
}

template <class T>
T lsb_mask(unsigned n)
{
	if (!n)
		return 0;

	return ~static_cast<T>(0) >> (std::numeric_limits<T>::digits - n);
}


using packed_rgb555 = im_p2p::pack_traits<
	uint8_t, uint16_t, im_p2p::little_endian_t, 1, 0,
	im_p2p::make_mask(im_p2p::C__, im_p2p::C_R, im_p2p::C_G, im_p2p::C_B),
	im_p2p::make_mask(0, 10, 5, 0),
	im_p2p::make_mask(1, 5, 5, 5)>;

enum class BitmapVersion {
	UNKNOWN,
	CORE,
	OS2,
	INFO,
	INFOV2,
	INFOV3,
	INFOV4,
	INFOV5,
};

// From Windows.h.
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef int32_t LONG;
typedef uint32_t DWORD;

enum {
	BI_RGB =       0,
	BI_RLE8 =      1,
	BI_RLE4 =      2,
	BI_BITFIELDS = 3,
	BI_JPEG =      4,
	BI_PNG =       5,
};

#pragma pack(push, 2)
typedef LONG FXPT2DOT30, *LPFXPT2DOT30;

typedef struct tagRGBQUAD {
	BYTE rgbBlue;
	BYTE rgbGreen;
	BYTE rgbRed;
	BYTE rgbReserved;
} RGBQUAD;

typedef struct tagCIEXYZ
{
	FXPT2DOT30 ciexyzX;
	FXPT2DOT30 ciexyzY;
	FXPT2DOT30 ciexyzZ;
} CIEXYZ;
typedef CIEXYZ *LPCIEXYZ;

typedef struct tagICEXYZTRIPLE
{
	CIEXYZ  ciexyzRed;
	CIEXYZ  ciexyzGreen;
	CIEXYZ  ciexyzBlue;
} CIEXYZTRIPLE;
typedef CIEXYZTRIPLE *LPCIEXYZTRIPLE;

typedef struct tagBITMAPFILEHEADER {
	WORD  bfType;
	DWORD bfSize;
	WORD  bfReserved1;
	WORD  bfReserved2;
	DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;

typedef struct tagBITMAPCOREHEADER {
	DWORD bcSize;
	WORD  bcWidth;
	WORD  bcHeight;
	WORD  bcPlanes;
	WORD  bcBitCount;
} BITMAPCOREHEADER, *PBITMAPCOREHEADER;

typedef struct {
	DWORD        biSize;
	LONG         biWidth;
	LONG         biHeight;
	WORD         biPlanes;
	WORD         biBitCount;
	// Since BITMAPINFOHEADER.
	DWORD        biCompression;
	DWORD        biSizeImage;
	LONG         biXPelsPerMeter;
	LONG         biYPelsPerMeter;
	DWORD        biClrUsed;
	DWORD        biClrImportant;
	// Since BITMAPV2INFOHEADER.
	DWORD        bV2RedMask;
	DWORD        bV2GreenMask;
	DWORD        bV2BlueMask;
	// Since BITMAPV3INFOHEADER.
	DWORD        bV3AlphaMask;
	// Since BITMAPV4HEADER.
	DWORD        bV4CSType;
	CIEXYZTRIPLE bV4Endpoints;
	DWORD        bV4GammaRed;
	DWORD        bV4GammaGreen;
	DWORD        bV4GammaBlue;
	// Since BITMAPV5HEADER.
	DWORD        bV5Intent;
	DWORD        bV5ProfileData;
	DWORD        bV5ProfileSize;
	DWORD        bV5Reserved;
} BITMAPV5HEADER, *LPBITMAPV5HEADER, *PBITMAPV5HEADER;
#pragma pack(pop)

static_assert(sizeof(BITMAPFILEHEADER) == 14, "wrong BITMAPFILEHEADER size");
static_assert(sizeof(BITMAPCOREHEADER) == BITMAPCOREHEADER_SIZE, "wrong BITMAPCOREHEADER size");
static_assert(sizeof(BITMAPV5HEADER) == BITMAPV5HEADER_SIZE, "wrong BITMAPV5HEADER size");

void bfheader_to_le(BITMAPFILEHEADER &bf)
{
	bf.bfType      = to_le(bf.bfType);
	bf.bfSize      = to_le(bf.bfSize);
	bf.bfReserved1 = to_le(bf.bfReserved1);
	bf.bfReserved2 = to_le(bf.bfReserved2);
	bf.bfOffBits   = to_le(bf.bfOffBits);
}

void bcheader_to_le(BITMAPCOREHEADER &bc)
{
	bc.bcSize     = to_le(bc.bcSize);
	bc.bcWidth    = to_le(bc.bcWidth);
	bc.bcHeight   = to_le(bc.bcHeight);
	bc.bcPlanes   = to_le(bc.bcPlanes);
	bc.bcBitCount = to_le(bc.bcBitCount);
}

void biheader_to_le(BITMAPV5HEADER &bi)
{
	bi.biSize          = to_le(bi.biSize);
	bi.biWidth         = to_le(bi.biWidth);
	bi.biHeight        = to_le(bi.biHeight);
	bi.biPlanes        = to_le(bi.biPlanes);
	bi.biBitCount      = to_le(bi.biBitCount);
	bi.biCompression   = to_le(bi.biCompression);
	bi.biSizeImage     = to_le(bi.biSizeImage);
	bi.biXPelsPerMeter = to_le(bi.biXPelsPerMeter);
	bi.biYPelsPerMeter = to_le(bi.biYPelsPerMeter);
	bi.biClrUsed       = to_le(bi.biClrUsed);
	bi.biClrImportant  = to_le(bi.biClrImportant);
	bi.bV2RedMask      = to_le(bi.bV2RedMask);
	bi.bV2GreenMask    = to_le(bi.bV2GreenMask);
	bi.bV2BlueMask     = to_le(bi.bV2BlueMask);
	bi.bV3AlphaMask    = to_le(bi.bV3AlphaMask);
	bi.bV4CSType       = to_le(bi.bV4CSType);

	bi.bV4Endpoints.ciexyzRed.ciexyzX   = to_le(bi.bV4Endpoints.ciexyzRed.ciexyzX);
	bi.bV4Endpoints.ciexyzRed.ciexyzY   = to_le(bi.bV4Endpoints.ciexyzRed.ciexyzY);
	bi.bV4Endpoints.ciexyzRed.ciexyzZ   = to_le(bi.bV4Endpoints.ciexyzRed.ciexyzZ);
	bi.bV4Endpoints.ciexyzGreen.ciexyzX = to_le(bi.bV4Endpoints.ciexyzGreen.ciexyzX);
	bi.bV4Endpoints.ciexyzGreen.ciexyzY = to_le(bi.bV4Endpoints.ciexyzGreen.ciexyzY);
	bi.bV4Endpoints.ciexyzGreen.ciexyzZ = to_le(bi.bV4Endpoints.ciexyzGreen.ciexyzZ);
	bi.bV4Endpoints.ciexyzBlue.ciexyzX  = to_le(bi.bV4Endpoints.ciexyzBlue.ciexyzX);
	bi.bV4Endpoints.ciexyzBlue.ciexyzY  = to_le(bi.bV4Endpoints.ciexyzBlue.ciexyzY);
	bi.bV4Endpoints.ciexyzBlue.ciexyzZ  = to_le(bi.bV4Endpoints.ciexyzBlue.ciexyzZ);

	bi.bV4GammaRed    = to_le(bi.bV4GammaRed);
	bi.bV4GammaGreen  = to_le(bi.bV4GammaGreen);
	bi.bV4GammaBlue   = to_le(bi.bV4GammaBlue);
	bi.bV5Intent      = to_le(bi.bV5Intent);
	bi.bV5ProfileData = to_le(bi.bV5ProfileData);
	bi.bV5ProfileSize = to_le(bi.bV5ProfileSize);
	bi.bV5Reserved    = to_le(bi.bV5Reserved);
}


void discard_from_io(IOContext *io, IOContext::size_type count)
{
	char buf[1024];

	while (count) {
		IOContext::size_type n = std::min(count, static_cast<IOContext::size_type>(sizeof(buf)));
		io->read_all(buf, n);
		count -= n;
	}
}

bool is_bmp_extension(const char *path)
{
	const char *ptr = strrchr(path, '.');
	if (!ptr)
		return false;

	for (const char *ext : BMP_EXTENSIONS) {
		if (!strcmp(ptr, ext))
			return true;
	}
	return false;
}

bool recognize_bmp(IOContext *io)
{
	uint8_t vec[2];
	IOContext::difference_type pos = io->tell();
	bool ret = false;

	io->read_all(vec, sizeof(vec));
	// BMP marker bytes.
	if (vec[0] == 'B' && vec[1] == 'M')
		ret = true;

	io->seek_set(pos);
	return ret;
}

BitmapVersion check_bi_size(DWORD sz)
{
	switch (sz) {
	case BITMAPCOREHEADER_SIZE:
		return BitmapVersion::CORE;
	case OS22XBITMAPHEADER_SIZE:
		return BitmapVersion::OS2;
	case BITMAPINFOHEADER_SIZE:
		return BitmapVersion::INFO;
	case BITMAPV2INFOHEADER_SIZE:
		return BitmapVersion::INFOV2;
	case BITMAPV3INFOHEADER_SIZE:
		return BitmapVersion::INFOV3;
	case BITMAPV4HEADER_SIZE:
		return BitmapVersion::INFOV4;
	case BITMAPV5HEADER_SIZE:
		return BitmapVersion::INFOV5;
	default:
		return BitmapVersion::UNKNOWN;
	}
}

std::pair<unsigned, unsigned> decode_bitfield(DWORD bitfield)
{
	if (!bitfield)
		return{ 0, 0 };

	return{ bsr(bitfield) - bsf(bitfield), bsf(bitfield) };
}

template <unsigned N>
void depalettize(void * const dst[3], const void *src, DWORD width, const RGBQUAD pal[256])
{
	const unsigned mask = lsb_mask<unsigned>(N);

	uint8_t *dst_p[3] = { static_cast<uint8_t *>(dst[0]), static_cast<uint8_t *>(dst[1]), static_cast<uint8_t *>(dst[2]) };
	const uint8_t *src_p = static_cast<const uint8_t *>(src);

	for (DWORD i = 0; i < width; ++i) {
		unsigned x = (src_p[(i * N) / 8] >> (8 - N - (i * N) % 8)) & mask;
		RGBQUAD val = pal[x];

		dst_p[0][i] = val.rgbRed;
		dst_p[1][i] = val.rgbGreen;
		dst_p[2][i] = val.rgbBlue;
	}
}

template <class T>
void unpack_bitfield(const void *src, void * const dst[4], DWORD width, std::pair<unsigned, unsigned> spec[4])
{
	const T *src_p = static_cast<const T *>(src);
	uint8_t *dst_p[4] = {
		static_cast<uint8_t *>(dst[0]), static_cast<uint8_t *>(dst[1]), static_cast<uint8_t *>(dst[2]), static_cast<uint8_t *>(dst[3]),
	};

	for (DWORD i = 0; i < width; ++i) {
		T x = to_le(src_p[i]);
		dst_p[0][i] = (x >> spec[0].second) & lsb_mask<T>(spec[0].first);
		dst_p[1][i] = (x >> spec[1].second) & lsb_mask<T>(spec[1].first);
		dst_p[2][i] = (x >> spec[2].second) & lsb_mask<T>(spec[2].first);

		if (dst_p[3] && spec[3].first)
			dst_p[3][i] = (x >> spec[3].second) & lsb_mask<T>(spec[3].first);
	}
}


class BMPDecoder : public ImageDecoder {
	BITMAPFILEHEADER m_bmp_file_header;
	BITMAPV5HEADER m_bmp_info_header;
	BitmapVersion m_bmp_version;
	RGBQUAD m_palette[256];

	ImageDecoderRegistry m_nested_registry;
	std::unique_ptr<ImageDecoder> m_nested_decoder;
	std::unique_ptr<IOContext> m_io;
	FileFormat m_format;
	bool m_alive;

	void decode_header()
	{
		if (!m_alive)
			return;

		m_io->read_all(&m_bmp_file_header, sizeof(m_bmp_file_header));
		bfheader_to_le(m_bmp_file_header);

		if (m_bmp_file_header.bfType != BITMAP_MAGIC)
			throw error::CannotDecodeImage{ "not a BMP file" };

		DWORD biSize;
		m_io->read_all(&biSize, sizeof(biSize));
		biSize = to_le(biSize);

		m_bmp_version = check_bi_size(biSize);
		if (m_bmp_version == BitmapVersion::UNKNOWN)
			throw error::CannotDecodeImage{ "unrecognized biSize value" };

		if (biSize == BITMAPCOREHEADER_SIZE) {
			BITMAPCOREHEADER bch;
			m_io->read_all(reinterpret_cast<uint8_t *>(&bch) + offsetof(BITMAPCOREHEADER, bcWidth),
			               biSize - sizeof(biSize));
			bcheader_to_le(bch);

			m_bmp_info_header.biSize          = BITMAPINFOHEADER_SIZE;
			m_bmp_info_header.biWidth         = bch.bcWidth;
			m_bmp_info_header.biHeight        = bch.bcHeight;
			m_bmp_info_header.biPlanes        = bch.bcPlanes;
			m_bmp_info_header.biBitCount      = bch.bcBitCount;
			m_bmp_info_header.biCompression   = BI_RGB;
			m_bmp_info_header.biSizeImage     = 0;
			m_bmp_info_header.biXPelsPerMeter = 0;
			m_bmp_info_header.biYPelsPerMeter = 0;
			m_bmp_info_header.biClrUsed       = 0;
			m_bmp_info_header.biClrImportant  = 0;

			m_bmp_version = BitmapVersion::INFO;
		} else {
			bool is_os2 = m_bmp_version == BitmapVersion::OS2;

			// Handle OS/2 bitmaps by skipping the additional (informational) fields.
			if (is_os2)
				biSize = BITMAPINFOHEADER_SIZE;

			m_io->read_all(reinterpret_cast<uint8_t *>(&m_bmp_info_header) + sizeof(biSize), biSize - sizeof(biSize));
			biheader_to_le(m_bmp_info_header);
			m_bmp_info_header.biSize = biSize;

			if (is_os2) {
				discard_from_io(m_io.get(), OS22XBITMAPHEADER_SIZE - BITMAPINFOHEADER_SIZE);
				m_bmp_version = BitmapVersion::INFO;
			}
		}

		unsigned palette_len = 0;
		unsigned depth = 0;

		switch (m_bmp_info_header.biBitCount) {
		case 1:
			// Do bit-expansion from monochrome to 8-bit.
			palette_len = 2;
			depth = 8;
			break;
		case 4:
			palette_len = 16;
			depth = 8;
			break;
		case 8:
			palette_len = 256;
			depth = 8;
			break;
		case 16:
			depth = 5;
			break;
		case 24:
		case 32:
			depth = 8;
			break;
		default:
			throw error::CannotDecodeImage{ "unknown biBitCount" };
		}

		if (m_bmp_info_header.biCompression != BI_RGB && m_bmp_info_header.biCompression != BI_BITFIELDS)
			throw error::CannotDecodeImage{ "BMP compression not supported" };
		if (m_bmp_info_header.biCompression == BI_RLE8 && m_bmp_info_header.biBitCount != 8)
			throw error::CannotCreateCodec{ "BI_RLE8 requires 8-bit bitmap" };
		if (m_bmp_info_header.biCompression == BI_RLE4 && m_bmp_info_header.biBitCount != 4)
			throw error::CannotDecodeImage{ "BI_RLE4 requires 4-bit bitmap" };
		if (m_bmp_info_header.biCompression == BI_BITFIELDS && m_bmp_info_header.biBitCount != 16 && m_bmp_info_header.biBitCount != 32)
			throw error::CannotDecodeImage{ "BI_BITFIELDS requires 16 or 32-bit bitmap" };

		if (m_bmp_info_header.biWidth < 0)
			throw error::CannotDecodeImage{ "negative width" };
		if (m_bmp_info_header.biBitCount <= 8 && m_bmp_info_header.biHeight < 0)
			throw error::CannotDecodeImage{ "paletted top-down DIB not allowed" };

		if (m_bmp_info_header.biCompression == BI_BITFIELDS && m_bmp_version == BitmapVersion::INFO) {
			// Read the color masks from the palette and store them in the BITMAPV2INFOHEADER fields.
			m_io->read_all(reinterpret_cast<char *>(&m_bmp_info_header) + offsetof(BITMAPV5HEADER, bV2RedMask),
			               offsetof(BITMAPV5HEADER, bV3AlphaMask) - offsetof(BITMAPV5HEADER, bV2RedMask));

			m_bmp_info_header.bV2RedMask   = to_le(m_bmp_info_header.bV2RedMask);
			m_bmp_info_header.bV2GreenMask = to_le(m_bmp_info_header.bV2GreenMask);
			m_bmp_info_header.bV2BlueMask  = to_le(m_bmp_info_header.bV2BlueMask);

			m_bmp_info_header.biSize = BITMAPV2INFOHEADER_SIZE;
			m_bmp_version = BitmapVersion::INFOV2;
		}

		m_io->read_all(&m_palette, sizeof(m_palette[0]) * palette_len);

		m_format.color_family = ColorFamily::RGB;
		m_format.plane_count = 3;
		for (unsigned p = 0; p < m_format.plane_count; ++p) {
			m_format.plane[p].width = m_bmp_info_header.biWidth;
			m_format.plane[p].height = m_bmp_info_header.biHeight;
			m_format.plane[p].bit_depth = depth;
		}

		if (m_bmp_info_header.biCompression == BI_BITFIELDS) {
			if (m_bmp_info_header.biBitCount == 16) {
				const DWORD high_mask = 0xFFFF0000UL;

				if ((m_bmp_info_header.bV2RedMask & high_mask) || (m_bmp_info_header.bV2GreenMask & high_mask) || (m_bmp_info_header.bV2BlueMask & high_mask))
					throw error::CannotDecodeImage{ "high WORD set in 16-bit BI_BITFIELDS" };
				if (m_bmp_version >= BitmapVersion::INFOV3 && (m_bmp_info_header.bV3AlphaMask & high_mask))
					throw error::CannotDecodeImage{ "high WORD set in 16-bit BI_BITFIELDS" };
			}

			m_format.plane[0].bit_depth = decode_bitfield(m_bmp_info_header.bV2RedMask).first;
			m_format.plane[1].bit_depth = decode_bitfield(m_bmp_info_header.bV2GreenMask).first;
			m_format.plane[2].bit_depth = decode_bitfield(m_bmp_info_header.bV2BlueMask).first;

			if (!m_format.plane[0].bit_depth || !m_format.plane[1].bit_depth || !m_format.plane[2].bit_depth)
				throw error::CannotDecodeImage{ "RGB channels required in BI_BITFIELDS" };

			if (m_bmp_version >= BitmapVersion::INFOV3) {
				unsigned depth = decode_bitfield(m_bmp_info_header.bV3AlphaMask).first;
				if (depth) {
					m_format.plane[3].bit_depth = depth;
					m_format.plane_count = 4;
					m_format.color_family = ColorFamily::RGBA;
				}
			}
		}

		if (!m_io->seekable()) {
			if (m_io->tell() > m_bmp_file_header.bfOffBits)
				throw error::CannotDecodeImage{ "incorrect bfOffBits" };
			discard_from_io(m_io.get(), m_bmp_file_header.bfOffBits - m_io->tell());
		} else {
			m_io->seek_set(m_bmp_file_header.bfOffBits);
		}

		if (m_bmp_info_header.biCompression == BI_JPEG || m_bmp_info_header.biCompression == BI_PNG) {
			FileFormat nested_format{ m_bmp_info_header.biCompression == BI_JPEG ? ImageType::JPEG : ImageType::PNG };
			m_nested_decoder = m_nested_registry.create_decoder("", &nested_format, std::move(m_io));
			if (!m_nested_decoder)
				throw error::CannotDecodeImage{ "no codec available for nested JPEG/PNG in BMP" };
		}
	}

	void decode_pal(const OutputBuffer &buffer) try
	{
		_im_assert_d(m_bmp_info_header.biWidth >= 0, "bad biWidth");
		_im_assert_d(m_bmp_info_header.biHeight >= 0, "bad biHeight");
		_im_assert_d(m_bmp_info_header.biCompression == BI_RGB, "compression not implemented");

		size_t rowsize = ceil_n((static_cast<size_t>(m_bmp_info_header.biWidth) * m_bmp_info_header.biBitCount + 7) / 8, sizeof(DWORD));
		std::vector<uint8_t> row_data(rowsize);

		if (static_cast<size_t>(PTRDIFF_MAX) / rowsize < static_cast<size_t>(m_bmp_info_header.biHeight))
			throw error::OutOfMemory{};

		for (LONG i = 0; i < m_bmp_info_header.biHeight; ++i) {
			LONG dib_row = m_bmp_info_header.biHeight - i - 1;
			void *dst_p[3];

			dst_p[0] = static_cast<uint8_t *>(buffer.data[0]) + dib_row * buffer.stride[0];
			dst_p[1] = static_cast<uint8_t *>(buffer.data[1]) + dib_row * buffer.stride[1];
			dst_p[2] = static_cast<uint8_t *>(buffer.data[2]) + dib_row * buffer.stride[2];

			// TODO: Implement RLE4 and RLE8.
			m_io->read_all(row_data.data(), rowsize);

			if (m_bmp_info_header.biBitCount == 1)
				depalettize<1>(dst_p, row_data.data(), m_bmp_info_header.biWidth, m_palette);
			else if (m_bmp_info_header.biBitCount == 4)
				depalettize<4>(dst_p, row_data.data(), m_bmp_info_header.biWidth, m_palette);
			else if (m_bmp_info_header.biBitCount == 8)
				depalettize<8>(dst_p, row_data.data(), m_bmp_info_header.biWidth, m_palette);
			else
				_im_assert_d(false, "bad biBitCount");
		}
	} catch (const std::bad_alloc &) {
		throw error::OutOfMemory{};
	}

	void decode_rgb(const OutputBuffer &buffer) try
	{
		_im_assert_d(m_bmp_info_header.biWidth >= 0, "bad biWidth");
		_im_assert_d(m_bmp_info_header.biCompression == BI_RGB || m_bmp_info_header.biCompression == BI_BITFIELDS, "compression not implemented");

		size_t rowsize = ceil_n(static_cast<size_t>(m_bmp_info_header.biWidth) * (m_bmp_info_header.biBitCount / 8), sizeof(DWORD));
		std::vector<uint8_t> row_data(rowsize);

		if (static_cast<size_t>(PTRDIFF_MAX) / rowsize < static_cast<size_t>(m_bmp_info_header.biHeight))
			throw error::OutOfMemory{};

		std::pair<unsigned, unsigned> bitfield_spec[4] = {};
		if (m_bmp_info_header.biCompression == BI_BITFIELDS) {
			bitfield_spec[0] = decode_bitfield(m_bmp_info_header.bV2RedMask);
			bitfield_spec[1] = decode_bitfield(m_bmp_info_header.bV2GreenMask);
			bitfield_spec[2] = decode_bitfield(m_bmp_info_header.bV2BlueMask);

			if (m_bmp_version >= BitmapVersion::INFOV3)
				bitfield_spec[3] = decode_bitfield(m_bmp_info_header.bV3AlphaMask);
		}

		DWORD height = std::labs(m_bmp_info_header.biHeight);
		for (DWORD i = 0; i < height; ++i) {
			DWORD dib_row = m_bmp_info_header.biHeight >= 0 ? height - i - 1 : i;
			void *dst_p[MAX_PLANE_COUNT] = {};

			dst_p[0] = static_cast<uint8_t *>(buffer.data[0]) + dib_row * buffer.stride[0];
			dst_p[1] = static_cast<uint8_t *>(buffer.data[1]) + dib_row * buffer.stride[1];
			dst_p[2] = static_cast<uint8_t *>(buffer.data[2]) + dib_row * buffer.stride[2];
			if (m_bmp_info_header.biCompression == BI_BITFIELDS && bitfield_spec[3].first)
				dst_p[3] = static_cast<uint8_t *>(buffer.data[3]) + dib_row * buffer.stride[3];

			m_io->read_all(row_data.data(), rowsize);

			if (m_bmp_info_header.biCompression == BI_BITFIELDS) {
				if (m_bmp_info_header.biBitCount == 16)
					unpack_bitfield<WORD>(row_data.data(), dst_p, m_bmp_info_header.biWidth, bitfield_spec);
				else if (m_bmp_info_header.biBitCount == 32)
					unpack_bitfield<DWORD>(row_data.data(), dst_p, m_bmp_info_header.biWidth, bitfield_spec);
				else
					_im_assert_d(false, "bad biBitCount");
			} else {
				if (m_bmp_info_header.biBitCount == 16)
					im_p2p::packed_to_planar<packed_rgb555>::unpack(row_data.data(), dst_p, 0, m_bmp_info_header.biWidth);
				else if (m_bmp_info_header.biBitCount == 24)
					im_p2p::packed_to_planar<im_p2p::packed_rgb24_le>::unpack(row_data.data(), dst_p, 0, m_bmp_info_header.biWidth);
				else if (m_bmp_info_header.biBitCount == 32)
					im_p2p::packed_to_planar<im_p2p::packed_argb32_le>::unpack(row_data.data(), dst_p, 0, m_bmp_info_header.biWidth);
				else
					_im_assert_d(false, "bad biBitCount");
			}
		}
	} catch (const std::bad_alloc &) {
		throw error::OutOfMemory{};
	}
public:
	explicit BMPDecoder(std::unique_ptr<IOContext> io) :
		m_bmp_file_header{},
		m_bmp_info_header{},
		m_bmp_version{ BitmapVersion::UNKNOWN },
		m_palette{},
		m_io{ std::move(io) },
		m_format{ ImageType::BMP, 1 },
		m_alive{ true }
	{
		m_nested_registry.register_default_providers();
	}

	const char *name() const override
	{
		return BMP_DECODER_NAME;
	}

	FileFormat file_format() override
	{
		if (m_bmp_version == BitmapVersion::UNKNOWN)
			decode_header();
		if (m_nested_decoder)
			return m_nested_decoder->file_format();

		return m_format;
	}

	FrameFormat next_frame_format() override
	{
		if (m_bmp_version == BitmapVersion::UNKNOWN)
			decode_header();
		if (m_nested_decoder)
			return m_nested_decoder->next_frame_format();

		return m_alive ? file_format() : FrameFormat{};
	}

	void decode(const OutputBuffer &buffer) override
	{
		if (m_nested_decoder) {
			m_nested_decoder->decode(buffer);
			return;
		}

		if (m_bmp_info_header.biBitCount <= 8)
			decode_pal(buffer);
		else
			decode_rgb(buffer);

		m_alive = false;
	}
};

} // namespace


const char *BMPDecoderFactory::name() const
{
	return BMP_DECODER_NAME;
}

int BMPDecoderFactory::priority() const
{
	return PRIORITY_NORMAL;
}

std::unique_ptr<ImageDecoder> BMPDecoderFactory::create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> &&io) try
{
	bool recognized;

	if (format)
		recognized = format->type == ImageType::BMP;
	else if (io->seekable())
		recognized = recognize_bmp(io.get());
	else
		recognized = is_bmp_extension(path);

	return recognized ? std::unique_ptr<ImageDecoder>{ new BMPDecoder{ std::move(io) } } : nullptr;
} catch (const std::bad_alloc &) {
	throw error::OutOfMemory{};
}

} // namespace imagine
