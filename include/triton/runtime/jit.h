#ifndef TDL_INCLUDE_JIT_H
#define TDL_INCLUDE_JIT_H

#include <string>
#include <memory>
#include "llvm/IR/LLVMContext.h"
#include "triton/ir/context.h"
#include "triton/ir/print.h"
#include "triton/driver/module.h"
#include "triton/driver/kernel.h"
#include "triton/codegen/selection.h"
#include "triton/codegen/tune.h"
#include "triton/codegen/optimize_dot.h"
#include "triton/codegen/optimize_dce.h"
#include "triton/codegen/optimize_trans.h"
#include "triton/codegen/shmem_allocation.h"
#include "triton/codegen/shmem_liveness.h"
#include "triton/codegen/shmem_info.h"
#include "triton/codegen/shmem_barriers.h"
#include "triton/codegen/alignment_info.h"
#include "triton/codegen/reassociate.h"
#include "triton/codegen/target.h"
#include "triton/codegen/vectorize.h"
#include "triton/runtime/launch_info.h"
#include <functional>

namespace llvm {
  class Module;
}

namespace triton {

namespace lang{
class translation_unit;
}

namespace codegen{
class tune;
}

namespace ir {
class module;
class context;
class metaparameter;
}

namespace runtime{

class jit {
public:
  typedef std::function<double(driver::kernel*, launch_information)> benchmark_t;

  struct tune_res_t{
    double perf;
    std::vector<unsigned> params;
  };

  struct passes_wrapper {
    passes_wrapper(codegen::target* target)
                    : shmem_liveness(&shmem_info),
                      shmem_allocation(&shmem_liveness, &shmem_info, &tune),
                      shmem_barriers(&shmem_allocation, &shmem_info),
                      vectorize(&tune),
                      selection(&shmem_allocation, &tune, &shmem_info, &alignment_info, target),
                      optimize_dot(&tune),
                      optimize_dce(),
                      optimize_trans(),
                      alignment_info(),
                      reassociate(&tune, &alignment_info),
                      target_(target) { }

    void target_independent(ir::module &module) {
      optimize_dot.run(module);
      optimize_trans.run(module);
      optimize_dce.run(module);
//      ir::print(module, std::cout);
    }

    void target_dependent(ir::module &module) {
      alignment_info.run(module);
//      reassociate.run(module);
      if(target_->is_gpu()){
        shmem_info.run(module);
        shmem_liveness.run(module);
        shmem_allocation.run();
        shmem_barriers.run(module);
      }
      vectorize.run(module);
      optimize_dce.run(module);
//      ir::print(module, std::cout);
    }

    codegen::tune tune;
    codegen::shmem_info shmem_info;
    codegen::shmem_liveness shmem_liveness;
    codegen::shmem_allocation shmem_allocation;
    codegen::shmem_barriers shmem_barriers;
    codegen::vectorize vectorize;
    codegen::selection selection;
    codegen::optimize_dot optimize_dot;
    codegen::optimize_dce optimize_dce;
    codegen::optimize_trans optimize_trans;
    codegen::alignment_info alignment_info;
    codegen::reassociate reassociate;
    codegen::target* target_;
  };

private:
  std::string compute_data_layout(bool is_64bit = true, bool use_short_pointers = true);
  std::unique_ptr<llvm::Module> make_llvm_module(triton::ir::module &module, passes_wrapper &passes, llvm::LLVMContext &context, launch_information &info);
  std::unique_ptr<ir::module> make_triton_module(const char *name, triton::ir::context &context, triton::lang::translation_unit *program);
  triton::lang::translation_unit *parse_program(const char *name, const char *src);

public:
  jit(driver::context* context, unsigned nthreads = 4);
  ~jit();
  std::vector<unsigned> get_valid(const char *name, const char *src);
  tune_res_t autotune(const char* name, const char* src, benchmark_t benchmark, const std::vector<std::vector<unsigned> > &targets = {});
  void add_module(ir::module &module, const std::vector<unsigned>& params = {});
  void add_module(const char* name, const char* src, const std::vector<unsigned>& params = {});
  driver::kernel* get_function(const char* name);
  launch_information get_launch_info(const char* name);

private:
  std::map<std::string, driver::module*> modules_;
  driver::context* driver_context_;
  llvm::LLVMContext llvm_context_;
  ir::context triton_context_;
  std::map<std::string, launch_information> launch_info_map_;
  std::shared_ptr<triton::codegen::target> target_;
  unsigned nthreads_;
};

}
}

#endif
