// generator_log.h
// Simple logging macro for generator TUs. By default only errors are
// printed. To enable debug/info prints define GENERATOR_VERBOSE=1 when
// compiling.
#pragma once
#include <iostream>

#if defined(GENERATOR_VERBOSE) && GENERATOR_VERBOSE == 1
// Accept a lambda or functor that writes to a stream: GEN_LOG_DEBUG([](){
// std::cerr<<"msg"; });
#define GEN_LOG_DEBUG(expr)                                                    \
  do {                                                                         \
    expr();                                                                    \
  } while (0)
#else
#define GEN_LOG_DEBUG(expr)                                                    \
  do {                                                                         \
  } while (0)
#endif
