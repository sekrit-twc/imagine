#pragma once

#ifndef IMAGINE_PNG_ENABLED
#define IMAGINE_PNG_ENABLED
#endif

#ifndef IMAGINE_PROVIDER_PNG_DECODER_H_
#define IMAGINE_PROVIDER_PNG_DECODER_H_

#ifdef IMAGINE_PNG_ENABLED

#include "common/decoder.h"

namespace imagine {

class PNGDecoderFactory : public ImageDecoderFactory {
public:
	const char *name() const override;

	int priority() const override;

	std::unique_ptr<ImageDecoder> create_decoder(const char *path, const FileFormat *format, std::unique_ptr<IOContext> &&io) override;
};


} // namespace imagine

#endif // IMAGINE_PNG_ENABLED
#endif // IMAGINE_PROVIDER_PNG_DECODER_H_
