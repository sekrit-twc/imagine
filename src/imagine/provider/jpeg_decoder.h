#pragma once

#ifndef IMAGINE_JPEG_ENABLED
#define IMAGINE_JPEG_ENABLED
#endif

#ifndef IMAGINE_PROVIDER_JPEG_DECODER_H_
#define IMAGINE_PROVIDER_JPEG_DECODER_H_

#ifdef IMAGINE_JPEG_ENABLED

#include "common/decoder.h"

namespace imagine {

class JPEGDecoderFactory : public ImageDecoderFactory {
public:
	const char *name() const override;

	int priority() const override;

	std::unique_ptr<ImageDecoder> create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> &&io) override;
};

} // imagine

#endif // IMAGINE_JPEG_ENABLED
#endif // IMAGINE_PROVIDER_JPEG_DECODER_H_
