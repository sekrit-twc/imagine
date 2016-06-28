#include <cassert>
#include <climits>
#include <cstring>
#include <exception>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vsxx/VapourSynth++.hpp>
#include <vsxx/vsxx_pluginmain.h>
#include "common/buffer.h"
#include "common/decoder.h"
#include "common/except.h"
#include "common/file_io.h"
#include "common/format.h"
#include "path.h"

using namespace vsxx;

namespace {

const unsigned FORMAT_DIGITS_MAX = std::numeric_limits<int>::digits10 - 1;

class FormatString {
	std::string m_prefix;
	std::string m_suffix;
	unsigned m_digits;
	bool m_zero_pad;
public:
	FormatString() : m_digits{}, m_zero_pad{}
	{
	}

	explicit FormatString(const std::string &str) : FormatString{}
	{
		std::regex pattern{ R"(^([^%]*)%(0*)([1-9][0-9]*)d(.*))" };
		std::smatch match;

		if (std::regex_match(str, match, pattern)) {
			m_prefix = match[1].str();
			m_zero_pad = match[2].length() != 0;
			if (match[3].length() > 2 || std::stod(match[3]) > FORMAT_DIGITS_MAX)
				throw std::runtime_error{ "too many digits in format string" };
			m_digits = std::stoi(match[3]);

			if (std::regex_match(match[4].str(), pattern))
				throw std::runtime_error{ "too many format specifiers" };
			m_suffix = match[4];
		} else {
			m_prefix = str;
		}
	}

	bool is_format_str() const
	{
		return m_digits != 0;
	}

	int max_index() const
	{
		int n = 1;
		for (unsigned i = 0; i < m_digits; ++i) {
			n *= 10;
		}
		return n;
	}

	std::string format(int n) const
	{
		std::string ret;
		std::string digits;
		ret.reserve(m_prefix.size() + m_digits + m_suffix.size());

		if (m_digits) {
			digits = std::to_string(n);
			if (m_zero_pad && digits.size() < m_digits)
				digits.insert(digits.begin(), m_digits - digits.size(), '0');
		}

		ret += m_prefix;
		ret += digits;
		ret += m_suffix;
		return ret;
	}
};

int get_sequence_length(const FormatString &fmt, int initial)
{
	std::string s;

	int imax = fmt.max_index();
	int count = 0;

	for (int i = initial; i < imax; ++i) {
		s = fmt.format(i);
		if (!path_exists(s))
			break;
		++count;
	}
	return count;
}

[[noreturn]] void translate_ioerror()
{
	try {
		throw;
	} catch (const imagine::error::IOError &e) {
		std::exception_ptr io_ex = std::current_exception();

		try {
			std::ostringstream ss;

			ss << e.what() << " path=" << e.path() << " off=" << e.off() << " count=" << e.count() << " code=" << e.error_code();
			throw std::runtime_error{ ss.str() };
		} catch (const std::bad_alloc &) {
			throw std::runtime_error{ "ioerror" };
		}
	}
}

VSColorFamily match_color_family(const imagine::FrameFormat &format)
{
	switch (format.color_family) {
	case imagine::ColorFamily::GRAY:
	case imagine::ColorFamily::GRAYALPHA:
		return cmGray;
	case imagine::ColorFamily::RGB:
	case imagine::ColorFamily::RGBA:
		return cmRGB;
	case imagine::ColorFamily::YUV:
	case imagine::ColorFamily::YUVA:
		return cmYUV;
	default:
		if (format.plane_count == 1)
			return cmGray;
		else if (format.plane_count == 3)
			return cmYUV;
		else
			throw std::runtime_error{ "unable to map color family" };
	}
}

bool has_alpha(const imagine::ColorFamily family)
{
	return family == imagine::ColorFamily::GRAYALPHA || family == imagine::ColorFamily::RGBA || family == imagine::ColorFamily::YUVA;
}

std::tuple<int, int, const VSFormat *> adjust_imformat(const imagine::FrameFormat &imformat, const VapourCore &core)
{
	unsigned w = imformat.plane[0].width;
	unsigned h = imformat.plane[0].height;
	unsigned depth = imformat.plane[0].bit_depth;
	VSSampleType st = imformat.plane[0].floating_point ? stFloat : stInteger;
	unsigned subsample_w = 0;
	unsigned subsample_h = 0;

	if (depth < 8 || depth > 32 || (st == stFloat && depth != 16 && depth != 32))
		throw std::runtime_error{ "unsupported bit depth" };

	if (imformat.plane_count >= 3 &&
		((imformat.plane[1].width != imformat.plane[2].width) ||
		 (imformat.plane[1].height != imformat.plane[2].height)))
		throw std::runtime_error{ "different U and V dimensions not supported" };
	if (!has_alpha(imformat.color_family) && imformat.plane_count >= 4)
		throw std::runtime_error{ "4-plane formats not supported" };

	VSColorFamily cf = match_color_family(imformat);
	if (cf == cmGray)
		return{ w, h, core.register_format(cf, st, depth, 0, 0) };

	for (unsigned p = 1; p < imformat.plane_count; ++p) {
		if (imformat.plane[p].width > w || imformat.plane[p].height > h)
			throw std::runtime_error{ "luma subsampling not allowed" };
		if (imformat.plane[p].bit_depth != depth || imformat.plane[p].floating_point && st != stFloat)
			throw std::runtime_error{ "per-plane bit depth not supported" };
	}

	for (unsigned ss = 1; ss < 3; ++ss) {
		unsigned ss_mod = 1 << ss;
		unsigned w_floor = w % ss_mod ? w - w % ss_mod : w;
		unsigned h_floor = h % ss_mod ? h - h % ss_mod : h;
		unsigned w_ceil = w % ss_mod ? (w + ss_mod - w % ss_mod) : w;
		unsigned h_ceil = h % ss_mod ? (h + ss_mod - h % ss_mod) : h;

		// Fix bad YUV images with wrong luma modulo.
		if (imformat.plane[1].width << ss == w_floor || imformat.plane[1].width << ss == w_ceil) {
			w = w_ceil;
			subsample_w = ss;
		}
		if (imformat.plane[1].height << ss == h_floor || imformat.plane[1].height << ss == h_ceil) {
			h = h_ceil;
			subsample_h = ss;
		}
	}
	if ((w != imformat.plane[1].width << subsample_w && w != (imformat.plane[1].width + 1) << subsample_w) ||
	    (h != imformat.plane[1].height << subsample_h && h != (imformat.plane[1].height + 1) << subsample_h))
		throw std::runtime_error{ "unsupported subsampling" };

	if (cf == cmRGB && (subsample_w || subsample_h))
		throw std::runtime_error{ "subsampled RGB not supported" };
	return{ w, h, core.register_format(cf, st, depth, subsample_w, subsample_h) };
}

void fix_bad_yuv_dimensions(const VideoFrame &vsframe, const imagine::FrameFormat &imformat)
{
	const VSFormat &vsformat = vsframe.format();

	for (unsigned p = 0; p < static_cast<unsigned>(vsformat.numPlanes); ++p) {
		unsigned w = vsframe.width(p);
		unsigned h = vsframe.height(p);

		if (w != imformat.plane[p].width || h != imformat.plane[p].height) {
			// Duplicate the last row.
			uint8_t *base_ptr = static_cast<uint8_t *>(vsframe.write_ptr(p));
			ptrdiff_t stride = vsframe.stride(p);

			const uint8_t *last_row = base_ptr + static_cast<ptrdiff_t>(imformat.plane[p].height - 1) * stride;
			for (unsigned i = imformat.plane[p].height; i < h; ++i) {
				memcpy(base_ptr + static_cast<ptrdiff_t>(i) * stride, last_row, w * vsformat.bytesPerSample);
			}
			// Duplicate the last column.
			for (unsigned i = 0; i < h; ++i) {
				uint8_t *row = base_ptr + static_cast<ptrdiff_t>(i) * stride;
				const uint8_t *sample = row + (imformat.plane[p].width - 1) * vsformat.bytesPerSample;

				for (unsigned j = imformat.plane[p].width; j < w; ++j) {
					memcpy(row + j * vsformat.bytesPerSample, sample, vsformat.bytesPerSample);
				}
			}
		}
	}
}

} // namespace


class ImageView : public FilterBase {
	imagine::ImageDecoderRegistry m_registry;
	imagine::FrameFormat m_imformat;
	FormatString m_format_str;
	VSVideoInfo m_vi;
	int m_initial;

	imagine::FrameFormat probe_image(const std::string &path) try
	{
		std::unique_ptr<imagine::FileIOContext> io{ new imagine::FileIOContext{ path } };
		std::unique_ptr<imagine::ImageDecoder> decoder = m_registry.create_decoder(path.c_str(), nullptr, std::move(io));

		if (!decoder)
			throw std::runtime_error{ "no decoder for format" };

		imagine::FrameFormat imformat = decoder->next_frame_format();
		if (!imagine::is_constant_format(imformat))
			throw std::runtime_error{ "decoder did not return a frame" };

		return imformat;
	} catch (const imagine::error::IOError &) {
		translate_ioerror();
	} catch (const imagine::error::Exception &e) {
		throw std::runtime_error{ e.what() };
	}

	VideoFrame decode_image(int n, const VapourCore &core) try
	{
		VideoFrame ret_frame;
		VideoFrame alpha_frame;

		std::unique_ptr<imagine::FileIOContext> io{ new imagine::FileIOContext{ m_format_str.format(m_initial + n) } };
		std::unique_ptr<imagine::ImageDecoder> decoder = m_registry.create_decoder(io->path(), nullptr, std::move(io));

		if (!decoder)
			throw std::runtime_error{ "no decoder for image" };

		imagine::FrameFormat imformat = decoder->next_frame_format();
		if (!imagine::is_constant_format(imformat))
			throw std::runtime_error{ "decoder did not return a frame" };

		auto f = adjust_imformat(imformat, core);
		int w = std::get<0>(f);
		int h = std::get<1>(f);
		const VSFormat *vsformat = std::get<2>(f);

		if ((m_vi.width && m_vi.height && m_vi.format) && (w != m_vi.width || h != m_vi.height || vsformat != m_vi.format))
			throw std::runtime_error{ "image format changed" };

		bool alpha = has_alpha(imformat.color_family);
		ret_frame = core.new_video_frame(*vsformat, w, h);
		if (alpha) {
			const VSFormat *alpha_vsformat = core.register_format(cmGray, static_cast<VSSampleType>(vsformat->sampleType), vsformat->bitsPerSample, 0, 0);
			alpha_frame = core.new_video_frame(*alpha_vsformat, w, h);
		}

		imagine::OutputBuffer imbuffer;
		for (int p = 0; p < vsformat->numPlanes; ++p) {
			imbuffer.data[p] = ret_frame.write_ptr(p);
			imbuffer.stride[p] = ret_frame.stride(p);
		}
		if (alpha) {
			imbuffer.data[imformat.plane_count - 1] = alpha_frame.write_ptr(0);
			imbuffer.stride[imformat.plane_count - 1] = alpha_frame.stride(0);
		}
		decoder->decode(imbuffer);
		fix_bad_yuv_dimensions(ret_frame, imformat);

		if (alpha)
			ret_frame.frame_props().set_prop("_Alpha", alpha_frame);

		return ret_frame;
	} catch (const imagine::error::IOError &) {
		translate_ioerror();
	} catch (const imagine::error::Exception &e) {
		throw std::runtime_error{ e.what() };
	}
public:
	ImageView(void * = nullptr) : m_vi{}, m_initial{}
	{
		m_registry.register_default_providers();
	}

	const char *get_name(int) noexcept override
	{
		return "rawview";
	}

	std::pair<::VSFilterMode, int> init(const ConstPropertyMap &in, const PropertyMap &out, const VapourCore &core) override
	{
		std::string path = path_canonicalize(in.get_prop<std::string>("path"));
		int64_t fpsnum = in.get_prop<int64_t>("fpsnum", map::Ignore{});
		int64_t fpsden = in.get_prop<int64_t>("fpsden", map::Ignore{});
		int initial = in.get_prop<int>("initial", map::default_val(-1));
		bool constant = in.get_prop<bool>("constant", map::default_val(true));

		if ((fpsnum <= 0) != (fpsden <= 0))
			throw std::runtime_error{ "must specify both fpsnum and fpsden" };
		if (fpsnum <= 0 && fpsden <= 0) {
			fpsnum = 25;
			fpsden = 1;
		}

		m_format_str = FormatString{ path };

		if (initial < 0 && path_exists(m_format_str.format(0)))
			initial = 0;
		else if (initial < 0 && path_exists(m_format_str.format(1)))
			initial = 1;
		else
			throw std::runtime_error{ "sequence not 0-based or 1-based" };

		m_initial = initial;

		int frame_count = get_sequence_length(m_format_str, initial);
		if (!frame_count)
			throw std::runtime_error{ "no files matching sequence" };

		if (fpsnum > 0 && fpsden > 0) {
			vs_normalizeRational(&fpsnum, &fpsden);
			m_vi.fpsNum = fpsnum;
			m_vi.fpsDen = fpsden;
		}

		m_vi.numFrames = frame_count;
		if (constant) {
			imagine::FrameFormat imformat = probe_image(m_format_str.format(initial));
			std::tie(m_vi.width, m_vi.height, m_vi.format) = adjust_imformat(imformat, core);
			m_imformat = imformat;
		}

		return{ fmSerial, 0 };
	}

	std::pair<const VSVideoInfo *, size_t> get_video_info() noexcept override
	{
		return{ &m_vi, 1 };
	}

	ConstVideoFrame get_frame_initial(int n, const VapourCore &core, VSFrameContext *) override
	{
		return decode_image(n, core);
	}

	ConstVideoFrame get_frame(int, const VapourCore &, VSFrameContext *) override
	{
		return ConstVideoFrame{};
	}
};

const PluginInfo g_plugin_info = {
	"com.imagine.imageview", "imageview", "Image Viewer", {
		{ &FilterBase::filter_create<ImageView>, "Source", "path:data;fpsnum:int:opt;fpsden:int:opt;initial:int:opt;constant:int:opt;" },
	}
};
