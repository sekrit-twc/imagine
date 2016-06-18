#pragma once

#ifndef IMAGINE_BUFFER_H_
#define IMAGINE_BUFFER_H_

#include <array>
#include <cstddef>
#include "common/format.h"

namespace imagine {

struct OutputBuffer {
	std::array<void *, MAX_PLANE_COUNT> data;
	std::array<ptrdiff_t, MAX_PLANE_COUNT> stride;

	OutputBuffer() : data{}, stride{}
	{
	}
};

struct InputBuffer {
	std::array<const void *, MAX_PLANE_COUNT> data;
	std::array<ptrdiff_t, MAX_PLANE_COUNT> stride;

	InputBuffer() : data{}, stride{}
	{
	}

	InputBuffer(const OutputBuffer &other) : data{}, stride(other.stride)
	{
		for (unsigned i = 0; i < data.size(); ++i) {
			data[i] = other.data[i];
		}
	}
};

} // namespace imagine

#endif // IMAGINE_BUFFER_H_
