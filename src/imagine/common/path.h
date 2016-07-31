#pragma once

#ifndef IMAGINE_PATH_H_
#define IMAGINE_PATH_H_

#include <cstddef>

namespace imagine {

bool is_matching_extension(const char *path, const char * const *extensions, size_t num_extensions);

} // namespace imagine

#endif // IMAGINE_PATH_H_
