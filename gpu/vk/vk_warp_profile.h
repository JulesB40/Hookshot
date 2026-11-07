// GPL-3.0-or-later. Vulkan stage bits differ from Eden's VertexA/Task/Mesh indices.
#pragma once
#include "vk_platform.h"
#include "shader_recompiler/stage.h"
namespace mkwvk {
inline uint32_t eden_subgroup_stages(VkShaderStageFlags stages) {
    uint32_t mask=0;
    const auto add=[&](Shader::Stage stage,VkShaderStageFlagBits bit) {
        if (stages&bit) mask|=1u<<static_cast<unsigned>(stage);
    };
    add(Shader::Stage::VertexA,VK_SHADER_STAGE_VERTEX_BIT);
    add(Shader::Stage::VertexB,VK_SHADER_STAGE_VERTEX_BIT);
    add(Shader::Stage::TessellationControl,VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    add(Shader::Stage::TessellationEval,VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    add(Shader::Stage::Geometry,VK_SHADER_STAGE_GEOMETRY_BIT);
    add(Shader::Stage::Fragment,VK_SHADER_STAGE_FRAGMENT_BIT);
    add(Shader::Stage::Compute,VK_SHADER_STAGE_COMPUTE_BIT);
    add(Shader::Stage::Task,VK_SHADER_STAGE_TASK_BIT_EXT);
    add(Shader::Stage::Mesh,VK_SHADER_STAGE_MESH_BIT_EXT);
    return mask;
}
}
