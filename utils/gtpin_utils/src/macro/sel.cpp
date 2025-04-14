#include "api/gtpin_api.h"
#include "capsule.hpp"
#include "def_gpu.hpp"

using namespace gtpin;
using namespace gtpin_prof;

/**
dst: register, src0: register, src1: register
*/

GtGenProcedure Macro::Sel(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                          const GtRegRegion& src0, const GtRegRegion& src1, GtExecMask execMask,
                          GtPredicate predicate) {
  MACRO_TRACING_3
  FTRACE_ASSERT(dst.DataType().Size() >= src0.DataType().Size() &&
             "Destination size should be no less than source size");
  FTRACE_ASSERT(dst.DataType().Size() >= src1.DataType().Size() &&
             "Destination size should be no less than source size");

  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();
  GtGenProcedure proc;
  proc += insF.MakeSel(dst, src0, src1, execMask).SetPredicate(predicate);
  return proc;
}

/**
dst: register, src0: register, src1: immediate
*/

GtGenProcedure Macro::Sel(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                          const GtRegRegion& src0, const GtImm& srcI1, GtExecMask execMask,
                          GtPredicate predicate) {
  MACRO_TRACING_3I
  FTRACE_ASSERT(dst.DataType().Size() >= src0.DataType().Size() &&
             "Destination size should be no less than source size");
  uint64_t mask = Macro::GetMaskBySizeBytes(dst.DataType().Size());
  FTRACE_ASSERT(srcI1.Value() <= mask && "Immediate value is too large for the destination size");

  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();
  GtGenProcedure proc;

  proc += insF.MakeSel(dst, src0, srcI1, execMask).SetPredicate(predicate);
  return proc;
}
