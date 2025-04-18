#include <map>

#include "api/gtpin_api.h"
#include "capsule.hpp"
#include "def_gpu.hpp"

using namespace gtpin;
using namespace gtpin_prof;

/**
dst: register, src0: register, src1: register
*/
GtGenProcedure AndTgl(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                      const GtRegRegion& src0, const GtRegRegion& src1, GtExecMask execMask,
                      GtPredicate predicate) {
  GtGenProcedure proc;
  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();

  if (dst.DataType().Size() == 8) {
    GtReg dstL = {dst.Reg(), 4, 0};
    GtReg dstH = {dst.Reg(), 4, 1};

    GtReg src0L = {src0.Reg(), std::min(static_cast<uint32_t>(sizeof(uint32_t)), src0.DataType().Size()), 0};
    GtReg src0H = NullReg();
    if (src0.DataType().Size() == 8) {
      src0H = {src0.Reg(), 4, 1};
    }

    GtReg src1L = {src1.Reg(), std::min(static_cast<uint32_t>(sizeof(uint32_t)), src1.DataType().Size()), 0};
    GtReg src1H = NullReg();
    if (src1.DataType().Size() == 8) {
      src1H = {src1.Reg(), 4, 1};
    }

    proc += insF.MakeAnd(dstL, src0L, src1L, execMask).SetPredicate(predicate);

    if (src0.DataType().Size() == 8 && src1.DataType().Size() == 8) {
      proc += insF.MakeAnd(dstH, src0H, src1H, execMask).SetPredicate(predicate);
    } else {
      proc += Macro::Mov(instrumentor, dstH, GtImm(0, GED_DATA_TYPE_ud), execMask, predicate);
    }

    return proc;
  }

  proc += insF.MakeAnd(dst, src0, src1, execMask).SetPredicate(predicate);
  return proc;
}

GtGenProcedure AndXeHpc(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                        const GtRegRegion& src0, const GtRegRegion& src1, GtExecMask execMask,
                        GtPredicate predicate) {
  GtGenProcedure proc;
  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();

  if (dst.DataType().Size() == 8 && src0.DataType().Size() == 8 && src1.DataType().Size() == 8) {
    GtReg dstL = {dst.Reg(), 4, 0};
    GtReg dstH = {dst.Reg(), 4, 1};

    GtReg src0L = {src0.Reg(), 4, 0};
    GtReg src0H = {src0.Reg(), 4, 1};

    GtReg src1L = {src1.Reg(), 4, 0};
    GtReg src1H = {src1.Reg(), 4, 1};

    proc += insF.MakeAnd(dstL, src0L, src1L, execMask).SetPredicate(predicate);
    proc += insF.MakeAnd(dstH, src0H, src1H, execMask).SetPredicate(predicate);
    return proc;
  }

  if (dst.DataType().Size() == 8 && src0.DataType().Size() >= 4 && src1.DataType().Size() == 4) {
    GtReg dstL = {dst.Reg(), 4, 0};
    GtReg dstH = {dst.Reg(), 4, 1};

    GtReg src0L = {src0.Reg(), 4, 0};

    proc += insF.MakeAnd(dstL, src0L, src1, execMask).SetPredicate(predicate);
    proc += insF.MakeMov(dstH, GtImm(0, GED_DATA_TYPE_ud), execMask).SetPredicate(predicate);
    return proc;
  }

  proc += insF.MakeAnd(dst, src0, src1, execMask).SetPredicate(predicate);
  return proc;
}

GtGenProcedure AndXe2(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                      const GtRegRegion& src0, const GtRegRegion& src1, GtExecMask execMask,
                      GtPredicate predicate) {
  GtGenProcedure proc;
  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();

  if (dst.DataType().Size() == 8 && src0.DataType().Size() == 8 && src1.DataType().Size() == 8) {
    GtReg dstL = {dst.Reg(), 4, 0};
    GtReg dstH = {dst.Reg(), 4, 1};

    GtReg src0L = {src0.Reg(), 4, 0};
    GtReg src0H = {src0.Reg(), 4, 1};

    GtReg src1L = {src1.Reg(), 4, 0};
    GtReg src1H = {src1.Reg(), 4, 1};

    proc += insF.MakeAnd(dstL, src0L, src1L, execMask).SetPredicate(predicate);
    proc += insF.MakeAnd(dstH, src0H, src1H, execMask).SetPredicate(predicate);
    return proc;
  }

  proc += insF.MakeAnd(dst, src0, src1, execMask).SetPredicate(predicate);
  return proc;
}

std::map<GED_MODEL,
         GtGenProcedure (*)(const IGtKernelInstrument&, const GtDstRegion&, const GtRegRegion&,
                            const GtRegRegion&, GtExecMask, GtPredicate)>
    AndFunctionsTable = {{GED_MODEL_TGL, &AndTgl},
                         {GED_MODEL_XE_HP, &AndXeHpc},
                         {GED_MODEL_XE_HPC, &AndXeHpc},
                         {GED_MODEL_XE2, &AndXe2}};

GtGenProcedure Macro::And(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                          const GtRegRegion& src0, const GtRegRegion& src1, GtExecMask execMask,
                          GtPredicate predicate) {
  MACRO_TRACING_3
  FTRACE_ASSERT(dst.DataType().Size() >= src0.DataType().Size() &&
             "Destination size should be no less than source size");
  FTRACE_ASSERT(dst.DataType().Size() >= src1.DataType().Size() &&
             "Destination size should be no less than source size");

#ifndef DISABLE_MACRO_WORKAROUNDS
  // check if there is a specific implementation for the current model
  GED_MODEL HwModel = instrumentor.Coder().GenModel().Id();
  if (AndFunctionsTable.find(HwModel) != AndFunctionsTable.end()) {
    return AndFunctionsTable[HwModel](instrumentor, dst, src0, src1, execMask, predicate);
  }
  // else default behaviour
#endif  // DISABLE_MACRO_WORKAROUNDS

  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();
  GtGenProcedure proc;
  proc += insF.MakeAnd(dst, src0, src1, execMask).SetPredicate(predicate);
  return proc;
}

/**
dst: register, src0: register, src1: immediate
*/

GtGenProcedure AndiTgl(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                       const GtRegRegion& src0, const GtImm& srcI1, GtExecMask execMask,
                       GtPredicate predicate) {
  GtGenProcedure proc;
  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();

  if (dst.DataType().Size() == 8) {
    GtReg dstL = {dst.Reg(), 4, 0};
    GtReg dstH = {dst.Reg(), 4, 1};
    GtReg src0L;
    GtReg src0H;

    if (src0.DataType().Size() <= 4) {
      src0H = NullReg();

      if (src0.DataType().Size() == 4) {
        src0L = {src0.Reg(), 4, 0};
      } else {
        auto& coder = instrumentor.Coder();
        auto& vregs = coder.VregFactory();
        GtReg tmpReg = vregs.MakeMsgDataScratch(VREG_TYPE_DWORD);
        proc += insF.MakeMov(tmpReg, src0, execMask).SetPredicate(predicate);
        src0L = tmpReg;
      }
    } else {  // src0.DataType().Size() == 8
      src0L = {src0.Reg(), 4, 0};
      src0H = {src0.Reg(), 4, 1};
    }

    proc += insF.MakeAnd(dstL, src0L, GtImm(srcI1.Value() & 0xFFFFFFFF, GED_DATA_TYPE_ud), execMask)
                .SetPredicate(predicate);
    proc += insF.MakeAnd(dstH, src0H, GtImm((srcI1.Value() >> 32) & 0xFFFFFFFF, GED_DATA_TYPE_ud),
                         execMask)
                .SetPredicate(predicate);

    return proc;
  }

  if (srcI1.DataType().Size() == 1) {
    proc += insF.MakeAnd(dst, src0, GtImm(srcI1.Value() & 0xFF, GED_DATA_TYPE_ud), execMask)
                .SetPredicate(predicate);
    return proc;
  }

  proc += insF.MakeAnd(dst, src0, srcI1, execMask).SetPredicate(predicate);
  return proc;
}

GtGenProcedure AndiXeHpc(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                         const GtRegRegion& src0, const GtImm& srcI1, GtExecMask execMask,
                         GtPredicate predicate) {
  GtGenProcedure proc;
  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();

  if (srcI1.DataType().Size() == 1) {
    proc += AndiXeHpc(instrumentor, dst, src0, GtImm(srcI1.Value() & 0xFF, GED_DATA_TYPE_ud),
                      execMask, predicate);
    return proc;
  }

  if (dst.DataType().Size() == 8 && src0.DataType().Size() == 8) {
    GtReg dstL = {dst.Reg(), 4, 0};
    GtReg dstH = {dst.Reg(), 4, 1};

    GtReg src0L = {src0.Reg(), 4, 0};
    GtReg src0H = {src0.Reg(), 4, 1};

    proc += insF.MakeAnd(dstL, src0L, GtImm(srcI1.Value() & 0xFFFFFFFF, GED_DATA_TYPE_ud), execMask)
                .SetPredicate(predicate);
    proc += insF.MakeAnd(dstH, src0H, GtImm((srcI1.Value() >> 32) & 0xFFFFFFFF, GED_DATA_TYPE_ud),
                         execMask)
                .SetPredicate(predicate);

    return proc;
  }

  proc += insF.MakeAnd(dst, src0,
                       GtImm(srcI1.Value(), Macro::GetGedIntDataTypeBytes(srcI1.DataType().Size())),
                       execMask)
              .SetPredicate(predicate);
  return proc;
}

GtGenProcedure AndiXe2(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                       const GtRegRegion& src0, const GtImm& srcI1, GtExecMask execMask,
                       GtPredicate predicate) {
  GtGenProcedure proc;
  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();

  if (srcI1.DataType().Size() == 1) {
    proc += insF.MakeAnd(dst, src0, GtImm(srcI1 & 0xFF, GED_DATA_TYPE_uw), execMask)
                .SetPredicate(predicate);
    return proc;
  }

  if (dst.DataType().Size() == 8 && src0.DataType().Size() == 8) {
    GtReg dstL = {dst.Reg(), 4, 0};
    GtReg dstH = {dst.Reg(), 4, 1};

    GtReg src0L = {src0.Reg(), 4, 0};
    GtReg src0H = {src0.Reg(), 4, 1};

    proc += insF.MakeAnd(dstL, src0L, GtImm(srcI1.Value() & 0xFFFFFFFF, GED_DATA_TYPE_ud), execMask)
                .SetPredicate(predicate);
    proc += insF.MakeAnd(dstH, src0H, GtImm((srcI1.Value() >> 32) & 0xFFFFFFFF, GED_DATA_TYPE_ud),
                         execMask)
                .SetPredicate(predicate);

    return proc;
  }

  proc += insF.MakeAnd(dst, src0, srcI1, execMask).SetPredicate(predicate);
  return proc;
}

std::map<GED_MODEL, GtGenProcedure (*)(const IGtKernelInstrument&, const GtDstRegion&,
                                       const GtRegRegion&, const GtImm&, GtExecMask, GtPredicate)>
    AndiFunctionsTable = {{GED_MODEL_TGL, &AndiTgl},
                          {GED_MODEL_XE_HP, &AndiXeHpc},
                          {GED_MODEL_XE_HPC, &AndiXeHpc},
                          {GED_MODEL_XE2, &AndiXe2}};

GtGenProcedure Macro::And(const IGtKernelInstrument& instrumentor, const GtDstRegion& dst,
                          const GtRegRegion& src0, const GtImm& srcI1, GtExecMask execMask,
                          GtPredicate predicate) {
  MACRO_TRACING_3I
  FTRACE_ASSERT(dst.DataType().Size() >= src0.DataType().Size() &&
             "Destination size should be no less than source size");
  uint64_t mask = Macro::GetMaskBySizeBytes(dst.DataType().Size());
  FTRACE_ASSERT(srcI1.Value() <= mask && "Immediate value is too large for the destination size");

  IGtInsFactory& insF = instrumentor.Coder().InstructionFactory();
  GtGenProcedure proc;

  if (srcI1 == 0) {
    proc += Macro::Mov(instrumentor, dst, GtImm(0, GED_DATA_TYPE_ud), execMask, predicate);
    return proc;
  }

#ifndef DISABLE_MACRO_WORKAROUNDS
  // check if there is a specific implementation for the current model
  GED_MODEL HwModel = instrumentor.Coder().GenModel().Id();
  if (AndiFunctionsTable.find(HwModel) != AndiFunctionsTable.end()) {
    return AndiFunctionsTable[HwModel](instrumentor, dst, src0, srcI1, execMask, predicate);
  }
  // else default behaviour
#endif  // DISABLE_MACRO_WORKAROUNDS

  proc += insF.MakeAnd(dst, src0, srcI1, execMask).SetPredicate(predicate);
  return proc;
}
