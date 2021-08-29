#include "taichi/ir/ir.h"
#include "taichi/ir/statements.h"
#include "taichi/ir/transforms.h"
#include "taichi/ir/analysis.h"
#include "taichi/transforms/make_mesh_attribute_local.h"

namespace taichi {
namespace lang {

const PassID MakeMeshAttributeLocal::id = "MakeMeshAttributeLocal";

namespace irpass {

void make_mesh_attribute_local_offload(OffloadedStmt *offload,
                              const CompileConfig &config,
                              const std::string &kernel_name) {
  if (offload->task_type != OffloadedStmt::TaskType::mesh_for) {
    return;
  }

  for (int j = 0; j < (int)offload->body->statements.size(); j++) {
    if (auto ifs = offload->body->statements[j]->cast<InternalFuncStmt>()) {
      if (ifs->func_name == "l2g") {
        VecStatement block;
        SNode *l2g = ifs->args[0]->cast<GlobalLoadStmt>()->src->cast<GlobalPtrStmt>()->snodes[0];
        SNode *offset = ifs->args[2]->cast<GlobalLoadStmt>()->src->cast<GlobalPtrStmt>()->snodes[0];
        auto get_load = [&](SNode *snode, Stmt* idx) {
          const auto lane = std::vector<Stmt*>{idx};
          Stmt* globalptr = block.push_back<GlobalPtrStmt>(LaneAttribute<SNode*>{snode}, lane);
          Stmt* load = block.push_back<GlobalLoadStmt>(globalptr);
          return load;
        };
        Stmt* patch_idx = block.push_back<MeshPatchIndexStmt>(offload);
        Stmt* total_v = get_load(offset, patch_idx);
        Stmt* index = block.push_back<BinaryOpStmt>(BinaryOpType::add, total_v, ifs->args[1]);
        Stmt* ans = get_load(l2g, index);
        ifs->replace_with(std::move(block));
      }
    }
  }
}

// This pass should happen after offloading but before lower_access
void make_mesh_attribute_local(IRNode *root,
                      const CompileConfig &config,
                      const MakeBlockLocalPass::Args &args) {
  TI_AUTO_PROF;

  if (auto root_block = root->cast<Block>()) {
    for (auto &offload : root_block->statements) {
      make_mesh_attribute_local_offload(offload->cast<OffloadedStmt>(), config,
                               args.kernel_name);
    }
  } else {
    make_mesh_attribute_local_offload(root->as<OffloadedStmt>(), config,
                             args.kernel_name);
  }
  type_check(root, config);
}

}  // namespace irpass
}  // namespace lang
}  // namespace taichi