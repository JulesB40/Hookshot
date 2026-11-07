#include "vk_warp_profile.h"
#include <cassert>
int main() {
    using Shader::Stage;
    const auto bit=[](Stage s) { return 1u<<static_cast<unsigned>(s); };
    assert(mkwvk::eden_subgroup_stages(0)==0);
    assert(mkwvk::eden_subgroup_stages(VK_SHADER_STAGE_COMPUTE_BIT)==bit(Stage::Compute));
    assert(mkwvk::eden_subgroup_stages(VK_SHADER_STAGE_VERTEX_BIT)==(bit(Stage::VertexA)|bit(Stage::VertexB)));
    assert(mkwvk::eden_subgroup_stages(VK_SHADER_STAGE_FRAGMENT_BIT)==bit(Stage::Fragment));
    assert(mkwvk::eden_subgroup_stages(VK_SHADER_STAGE_TASK_BIT_EXT)==bit(Stage::Task));
    assert(mkwvk::eden_subgroup_stages(VK_SHADER_STAGE_MESH_BIT_EXT)==bit(Stage::Mesh));
    assert(mkwvk::eden_subgroup_stages(VK_SHADER_STAGE_ALL)==0x1ff);
}
