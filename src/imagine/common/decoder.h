#pragma once

#ifndef IMAGINE_DECODER_H_
#define IMAGINE_DECODER_H_

#include <limits>
#include <map>
#include <memory>
#include "format.h"

namespace imagine {

struct OutputBuffer;
class IOContext;

class ImageDecoder {
	ImageDecoder(const ImageDecoder &) = delete;
	ImageDecoder &operator=(const ImageDecoder &) = delete;
protected:
	ImageDecoder() = default;
public:
	virtual ~ImageDecoder() = 0;

	virtual const char *name() const = 0;

	virtual FileFormat file_format() = 0;

	virtual FrameFormat next_frame_format() = 0;

	virtual void decode(const OutputBuffer &buffer) = 0;
};

class ImageDecoderFactory {
	ImageDecoderFactory(const ImageDecoderFactory &) = delete;
	ImageDecoderFactory &operator=(const ImageDecoderFactory &) = delete;
protected:
	ImageDecoderFactory() = default;
public:
	static const int PRIORITY_MAX = std::numeric_limits<int>::min();
	static const int PRIORITY_HIGH = -0x4000;
	static const int PRIORITY_NORMAL = 0;
	static const int PRIORITY_LOW = 0x4000;
	static const int PRIORITY_MIN = std::numeric_limits<int>::max();

	virtual ~ImageDecoderFactory() = 0;

	virtual const char *name() const = 0;

	virtual int priority() const = 0;

	virtual std::unique_ptr<ImageDecoder> create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> &&io) = 0;
};

class ImageDecoderRegistry {
	std::multimap<int, std::unique_ptr<ImageDecoderFactory>> m_registry;
public:
	void register_default_providers();

	void register_provider(std::unique_ptr<ImageDecoderFactory> factory);

	void disable_provider(const char *name);

	std::unique_ptr<ImageDecoder> create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> io);
};

} // namespace imagine

#endif // IMAGINE_DECODER_H_
