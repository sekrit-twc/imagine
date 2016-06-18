#undef _im_assert
#undef _im_assert_d

#ifdef NDEBUG
  #define IM_NDEBUG
  #undef NDEBUG
#endif

#include <assert.h>

#define _im_assert(x, msg) assert((x) && (msg))

#ifdef IM_NDEBUG
  #include "ccdep.h"
  #define _im_assert_d(x, msg) ASSUME_CONDITION(x)
  #undef IM_NDEBUG
  #define NDEBUG
#else
  #define _im_assert_d(x, msg) _im_assert(x, msg)
#endif
