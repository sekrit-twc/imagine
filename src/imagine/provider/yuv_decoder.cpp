#include <limits>
#include <utility>
#include "common/buffer.h"
#include "common/except.h"
#include "common/format.h"
#include "common/io_context.h"
#include "yuv_decoder.h"

namespace imagine {
namespace {

const char YUV_DECODER_NAME[] = "yuv";

size_t yuv_frame_size(const FileFormat& format)
{
	size_t size = 0;

	for (unsigned i = 0; i < MAX_PLANE_COUNT; ++i) {
		size_t w = format.plane[i].width;
		size_t h = format.plane[i].height;
		size_t byte_per_sample = (format.plane[i].bit_depth + 7) / 8;
		size += w * h * byte_per_sample;
	}
	return size;
}

class YUVDecoder : public ImageDecoder {
	FileFormat m_format;
	size_t m_frame_no;
	std::unique_ptr<IOContext> m_io;
public:
	explicit YUVDecoder(const FileFormat &format, std::unique_ptr<IOContext> &&io) :
		m_format{ format },
		m_frame_no{},
		m_io{ std::move(io) }
	{
		if (!m_format.frame_count && m_io->seekable())
			m_format.frame_count = static_cast<unsigned>(m_io->size() / yuv_frame_size(m_format));
	}

	const char *name() const override
	{
		return YUV_DECODER_NAME;
	}

	FileFormat file_format() override
	{
		return m_format;
	}

	FrameFormat next_frame_format() override
	{
		if ((m_format.frame_count && m_frame_no == m_format.frame_count) || m_io->eof())
			return FrameFormat{};

		return m_format;
	}

	void decode(const OutputBuffer &buffer) override
	{
		for (unsigned p = 0; p < m_format.plane_count; ++p) {
			char *data = static_cast<char *>(buffer.data[p]);

			for (unsigned i = 0; i < m_format.plane[p].height; ++i) {
				size_t rowsize = m_format.plane[p].width * ((m_format.plane[p].bit_depth + 7) / 8);
				m_io->read_all(data, rowsize);
				data += buffer.stride[p];
			}
		}
		++m_frame_no;
	}
};

} // namespace


const char *YUVDecoderFactory::name() const
{
	return YUV_DECODER_NAME;
}

int YUVDecoderFactory::priority() const
{
	// Lowest priority.
	return PRIORITY_MIN;
}

std::unique_ptr<ImageDecoder> YUVDecoderFactory::create_decoder(const char *, const FileFormat *format, std::unique_ptr<IOContext> &&io) try
{
	if (!format || format->type != ImageType::YUV_TEST || !is_constant_format(*format))
		return nullptr;

	return std::unique_ptr<ImageDecoder>{ new YUVDecoder{ *format, std::move(io) } };
} catch (const std::bad_alloc &) {
	throw error::OutOfMemory{};
}

} // namespace imagine
