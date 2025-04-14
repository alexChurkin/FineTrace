#ifndef FTRACE_UTILS_FTRACE_ASSERT_H_
#define FTRACE_UTILS_FTRACE_ASSERT_H_

#ifdef NDEBUG
#undef NDEBUG
#include <assert.h>
#define NDEBUG
#else
#include <assert.h>
#endif

#define FTRACE_ASSERT(X) assert(X)

#endif // FTRACE_UTILS_FTRACE_ASSERT_H_