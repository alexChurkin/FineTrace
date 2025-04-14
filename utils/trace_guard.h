#ifndef FTRACE_TOOLS_CL_TRACER_TRACE_GUARD_H_
#define FTRACE_TOOLS_CL_TRACER_TRACE_GUARD_H_

#include "finetrace_assert.h"

class TraceGuard {
 public:
  TraceGuard() {
    ++inactive_count_;
  }

  TraceGuard(const TraceGuard& that) = delete;

  ~TraceGuard() {
    FTRACE_ASSERT(inactive_count_ > 0);
    --inactive_count_;
  }

  static bool Inactive() {
    return inactive_count_ > 0;
  }

 private:
  static thread_local int inactive_count_;
};

#endif // FTRACE_TOOLS_CL_TRACER_TRACE_GUARD_H_
