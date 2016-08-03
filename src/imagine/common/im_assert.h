#undef im_assert
#undef im_assert_d

#ifdef NDEBUG
  #define IM_NDEBUG
  #undef NDEBUG
#endif

#include <assert.h>

#define im_assert(x, msg) assert((x) && (msg))

#ifdef IM_NDEBUG
  #include "ccdep.h"
  #define im_assert_d(x, msg) ASSUME_CONDITION(x)
  #undef IM_NDEBUG
  #define NDEBUG
#else
  #define im_assert_d(x, msg) im_assert(x, msg)
#endif
