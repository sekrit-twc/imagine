#include <cstring>
#include <utility>
#include "provider/bmp_decoder.h"
#include "provider/jpeg_decoder.h"
#include "provider/png_decoder.h"
#include "provider/tiff_decoder.h"
#include "decoder.h"
#include "except.h"
#include "io_context.h"
#include "im_assert.h"

namespace imagine {

ImageDecoder::~ImageDecoder() = default;

ImageDecoderFactory::~ImageDecoderFactory() = default;

void ImageDecoderRegistry::register_default_providers() try
{
#ifdef IMAGINE_JPEG_ENABLED
	register_provider(std::unique_ptr<ImageDecoderFactory>{ new JPEGDecoderFactory{} });
#endif
#ifdef IMAGINE_PNG_ENABLED
	register_provider(std::unique_ptr<ImageDecoderFactory>{ new PNGDecoderFactory{} });
#endif
#ifdef IMAGINE_TIFF_ENABLED
	register_provider(std::unique_ptr<ImageDecoderFactory>{ new TIFFDecoderFactory{} });
#endif
	register_provider(std::unique_ptr<ImageDecoderFactory>{ new BMPDecoderFactory{} });
} catch (const std::bad_alloc &) {
	throw error::OutOfMemory{};
}

void ImageDecoderRegistry::register_provider(std::unique_ptr<ImageDecoderFactory> factory) try
{
	m_registry.insert(std::make_pair(factory->priority(), std::move(factory)));
} catch (const std::bad_alloc &) {
	throw error::OutOfMemory{};
}

void ImageDecoderRegistry::disable_provider(const char *name)
{
	for (auto it = m_registry.begin(); it != m_registry.end();) {
		if (!strcmp(it->second->name(), name))
			it = m_registry.erase(it);
		else
			++it;
	}
}

std::unique_ptr<ImageDecoder> ImageDecoderRegistry::create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> io) const
{
	IOContext::difference_type pos = io->tell();
	for (const auto &factory : m_registry) {
		std::unique_ptr<ImageDecoder> provider = factory.second->create_decoder(path, format, std::move(io));
		if (provider)
			return provider;

		im_assert_d(io, "factory must not move IOContext");
		if (io->seekable())
			io->seek_set(pos);
	}
	return nullptr;
}

} // namespace imagine
