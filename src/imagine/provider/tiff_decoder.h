#pragma once

#ifndef IMAGINE_TIFF_ENABLED
#define IMAGINE_TIFF_ENABLED
#endif

#ifndef IMAGINE_PROVIDER_TIFF_DECODER_H_
#define IMAGINE_PROVIDER_TIFF_DECODER_H_

#include "common/decoder.h"

namespace imagine {

class TIFFDecoderFactory : public ImageDecoderFactory {
public:
	const char *name() const override;

	int priority() const override;

	std::unique_ptr<ImageDecoder> create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> &&io) override;
};

} // namespace imagine

#endif // IMAGINE_PROVIDER_TIFF_DECODER_H_
