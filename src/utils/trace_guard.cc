#include "trace_guard.h"

thread_local int TraceGuard::inactive_count_ = 0;