#pragma once

#ifndef IMAGINE_FORMAT_H_
#define IMAGINE_FORMAT_H_

namespace imagine {

const unsigned MAX_PLANE_COUNT = 4;

enum class ImageType {
	BMP,
	DPX,
	EXR,
	JPEG,
	JPEG2000,
	PNG,
	TIFF,
	YUV_TEST,
};

enum class ColorFamily {
	UNKNOWN,
	// Y = 0.
	GRAY,
	// Y = 0, U = 1, V = 2.
	YUV,
	// R = 0, G = 1, B = 2.
	RGB,
	// Y = 0, A = 1.
	GRAYALPHA,
	// Y = 0, U = 1, V = 2, A = 3.
	YUVA,
	// Y = 0, U = 1, V = 2, A = 3.
	RGBA,
	// Y = 0, Cb = 1, Cr = 2, K = 3.
	YCCK,
	// C = 0, M = 1, Y = 2, K = 3.
	CMYK,
};

struct PlaneFormat {
	unsigned width;
	unsigned height;
	unsigned bit_depth;
	bool floating_point;

	PlaneFormat() : width{}, height{}, bit_depth{}, floating_point{}
	{
	}

	PlaneFormat(unsigned width, unsigned height, unsigned bit_depth, bool floating_point = false) :
		width{ width },
		height{ height },
		bit_depth{ bit_depth },
		floating_point{ floating_point }
	{
	}
};

struct FrameFormat {
	PlaneFormat plane[MAX_PLANE_COUNT];
	unsigned plane_count;
	ColorFamily color_family;

	FrameFormat() : plane{}, plane_count{}, color_family{}
	{
	}
};

struct FileFormat : public FrameFormat {
	ImageType type;
	unsigned frame_count;

	explicit FileFormat(ImageType type, unsigned frame_count = 0) :
		type{ type },
		frame_count{ frame_count }
	{
	}
};

inline bool is_constant_format(const FrameFormat &format)
{
	return format.plane_count != 0;
}

inline bool is_chroma_plane(ColorFamily family, unsigned p)
{
	return (family == ColorFamily::YUV || family == ColorFamily::YCCK) && (p == 1 || p == 2);
}

} // namespace imagine

#endif // IMAGINE_FORMAT_H_
