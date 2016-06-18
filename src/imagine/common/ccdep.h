#pragma once

#ifndef IMAGINE_CCDEP_H_
#define IMAGINE_CCDEP_H_

#if defined(_MSC_VER)
  #define ASSUME_CONDITION(x) __assume((x))
#elif defined(__GNUC__)
  #define ASSUME_CONDITION(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#else
  #define ASSUME_CONDITION(x) ((void)0)
#endif

#endif /* IMAGINE_CCDEP_H_ */
