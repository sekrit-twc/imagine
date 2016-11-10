#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "common/align.h"
#include "common/buffer.h"
#include "common/decoder.h"
#include "common/except.h"
#include "common/file_io.h"
#include "common/io_context.h"
#include "aligned_malloc.h"
#include "argparse.h"

extern void *linktest_func();

namespace {

void _link()
{
	linktest_func();
}

class ManagedOutputBuffer {
	imagine::OutputBuffer m_buffer;

	void free_buffer()
	{
		for (unsigned i = 0; i < imagine::MAX_PLANE_COUNT; ++i) {
			aligned_free(m_buffer.data[i]);
			m_buffer.data[i] = nullptr;
		}
	}
public:
	ManagedOutputBuffer(const imagine::FrameFormat &format)
	{
		try {
			for (unsigned i = 0; i < format.plane_count; ++i) {
				unsigned plane_w = imagine::ceil_n(format.plane[i].width, imagine::ALIGNMENT);
				unsigned plane_h = format.plane[i].height;
				unsigned byte_size = imagine::ceil_n(format.plane[i].bit_depth, 8) / 8;

				if (((SIZE_MAX / 2) / byte_size) / plane_w < plane_h)
					throw std::bad_alloc{};

				m_buffer.data[i] = aligned_malloc(static_cast<size_t>(plane_w) * plane_h * byte_size, 32);
				m_buffer.stride[i] = plane_w;
			}
		} catch (...) {
			free_buffer();
			throw;
		}
	}

	ManagedOutputBuffer(const ManagedOutputBuffer &) = delete;

	~ManagedOutputBuffer()
	{
		free_buffer();
	}

	ManagedOutputBuffer &operator=(const ManagedOutputBuffer &) = delete;

	const imagine::OutputBuffer &buffer() const
	{
		return m_buffer;
	}
};

void print_imagine_error()
{
	try {
		throw;
	} catch (const imagine::error::IOError &e) {
		std::cerr << "IO error: path='" << e.path() << "' offset=" << e.off() << " count=" << e.count() << ' ' << e.what() << '\n';
		std::cerr << "reason: " << std::strerror(e.error_code()) << '\n';
	} catch (const imagine::error::Exception &e) {
		std::cerr << "imagine error: " << e.what() << '\n';
	}
}

std::ostream &operator<<(std::ostream &os, imagine::ImageType type)
{
	switch (type) {
	case imagine::ImageType::BMP:
		return os << "bmp";
	case imagine::ImageType::DPX:
		return os << "dpx";
	case imagine::ImageType::EXR:
		return os << "exr";
	case imagine::ImageType::JPEG:
		return os << "jpeg";
	case imagine::ImageType::JPEG2000:
		return os << "jpeg2000";
	case imagine::ImageType::PNG:
		return os << "png";
	case imagine::ImageType::TIFF:
		return os << "tiff";
	default:
		return os;
	}
}

std::ostream &operator<<(std::ostream &os, const imagine::PlaneFormat &plane)
{
	return os << plane.width << 'x' << plane.height;
}

std::ostream &operator<<(std::ostream &os, const imagine::FrameFormat &format)
{
	os << "planes:" << format.plane_count;

	if (!format.plane_count)
		return os;

	os << " [";
	for (unsigned i = 0; i < format.plane_count; ++i) {
		if (i)
			os << ' ';
		os << format.plane[i];
	}
	os << ']';
	return os;
}

std::ostream &operator<<(std::ostream &os, const imagine::FileFormat &format)
{
	return os << "type:" << format.type
	          << " frames:" << format.frame_count
	          << ' ' << static_cast<const imagine::FrameFormat &>(format);
}

void save_image(const imagine::InputBuffer &buffer, const imagine::FrameFormat &format, const char *prefix, unsigned n)
{
	std::ostringstream ss;
	ss << prefix;

	if (ss.str() != "NUL")
		ss << std::setfill('0') << std::setw(6) << n << ".bin";

	std::string path{ ss.str() };
	imagine::FileIOContext io{ path, imagine::FileIOContext::write_tag };

	for (unsigned p = 0; p < format.plane_count; ++p) {
		size_t rowsize = static_cast<size_t>(format.plane[p].width) * (imagine::ceil_n(format.plane[p].bit_depth, 8) / 8);
		const uint8_t *src_p = static_cast<const uint8_t *>(buffer.data[p]);

		for (unsigned i = 0; i < format.plane[p].height; ++i) {
			io.write_all(src_p, rowsize);
			src_p += buffer.stride[p];
		}
	}
}

struct Arguments {
	const char *inpath;
	const char *outprefix;
};

const ArgparseOption program_positional[] = {
	{ OPTION_STRING, nullptr, "inpath",    offsetof(Arguments, inpath),    nullptr, "input image path" },
	{ OPTION_STRING, nullptr, "outprefix", offsetof(Arguments, outprefix), nullptr, "output image outprefix" },
	{ OPTION_NULL }
};

const ArgparseCommandLine program_def = { nullptr, program_positional, "testapp", "decode images" };

} // namespace


int main(int argc, char **argv)
{
	Arguments args{};
	int ret;

	if ((ret = argparse_parse(&program_def, &args, argc, argv)) < 0)
		return ret == ARGPARSE_HELP_MESSAGE ? 0 : ret;

	// For linkage only.
	if (argc == -1)
		_link();

	try {
		imagine::ImageDecoderRegistry registry;
		registry.register_default_providers();

		std::unique_ptr<imagine::FileIOContext> file_io{ new imagine::FileIOContext{ argv[1] } };
		std::unique_ptr<imagine::ImageDecoder> decoder = registry.create_decoder(argv[1], nullptr, std::move(file_io));
		if (!decoder)
			throw std::runtime_error{ "no decoder for file" };

		imagine::FileFormat file_format = decoder->file_format();

		std::cout << "image decoder: " << decoder->name() << '\n';
		std::cout << file_format << '\n';

		unsigned decoded_count = 0;
		for (imagine::FrameFormat format = decoder->next_frame_format();
		     imagine::is_constant_format(format);
			 format = decoder->next_frame_format())
		{
			if (!imagine::is_constant_format(file_format))
				std::cout << "frame " << decoded_count << ": " << format << '\n';

			ManagedOutputBuffer buffer{ format };

			try {
				decoder->decode(buffer.buffer());
			} catch (const imagine::error::EndOfFile &) {
				std::cout << "eof on frame: " << decoded_count << '\n';
				break;
			}

			save_image(buffer.buffer(), format, args.outprefix, decoded_count);
			++decoded_count;
		}
		std::cout << "decoded " << decoded_count << " frames\n";
		ret = 0;
	} catch (const imagine::error::Exception &) {
		print_imagine_error();
		ret = 1;
	} catch (const std::exception &e) {
		std::cerr << "error: " << e.what() << '\n';
		ret = 1;
	}

	return ret;
}
