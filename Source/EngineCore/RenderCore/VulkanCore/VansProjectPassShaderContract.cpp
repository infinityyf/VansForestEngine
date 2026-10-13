#include "VansProjectPassShaderContract.h"
#include "VansDescriptorSetLayouts.h"
#include <spirv_cross/spirv_cross.hpp>
#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace VansGraphics
{
bool ValidateProjectPassShaderSpirv(const Vans::VansRenderPassAsset& definition,
    const std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>>& stages,
    std::uint32_t parameterBytes, VansProjectPassShaderSignature& signature, std::string& error)
{
    const auto fail = [&](const char* message) { error = definition.passId + ": " + message; return false; };
    if (!stages.count(VK_SHADER_STAGE_VERTEX_BIT)) return fail("DrawObjects requires vertex SPIR-V");
    const std::uint32_t colors = static_cast<std::uint32_t>(std::count_if(definition.targets.begin(), definition.targets.end(),
        [](const auto& target) { return target.format != Vans::VansRenderTargetFormat::D32; }));
    if (colors && !stages.count(VK_SHADER_STAGE_FRAGMENT_BIT)) return fail("Color attachments require fragment SPIR-V");
    VansProjectPassShaderSignature parsed;
    const auto globalBindings = VansDescriptorSetLayoutFactory::GlobalBindings();
    try
    {
        for (const auto& [stage, words] : stages)
        {
            if (stage != VK_SHADER_STAGE_VERTEX_BIT && stage != VK_SHADER_STAGE_FRAGMENT_BIT)
                return fail("Project DrawObjects currently supports vertex/fragment stages only");
            spirv_cross::Compiler compiler(words);
            const auto resources = compiler.get_shader_resources(compiler.get_active_interface_variables());
            if (!resources.push_constant_buffers.empty() || !resources.storage_images.empty() ||
                !resources.subpass_inputs.empty() || !resources.separate_images.empty() ||
                !resources.separate_samplers.empty() || !resources.atomic_counters.empty() || !resources.acceleration_structures.empty())
                return fail("Shader requests an undeclared or later-produced resource class");
            const auto validateResource = [&](const auto& resource, VkDescriptorType type) {
                const auto set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                const auto binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                if (set == DS_GLOBAL)
                {
                    // Tile-light, atmosphere, cloud, screen-space and probe-index results are produced after this stage.
                    if (binding == 9 || binding == 10 || (binding >= 20 && binding <= 37) || binding == 41 || binding == 42 ||
                        (binding > 42 && binding != 50)) return false;
                    const auto declared = std::find_if(globalBindings.begin(), globalBindings.end(), [&](const auto& b) { return b.binding == binding; });
                    if (declared == globalBindings.end() || declared->descriptorType != type || !(declared->stageFlags & stage)) return false;
                    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    {
                        const auto& image = compiler.get_type(resource.type_id).image;
                        const bool cube = binding == GLOBAL_BINDING_PRECONV_DIFFUSE ||
                            binding == GLOBAL_BINDING_PRECONV_SPECULAR || binding == GLOBAL_BINDING_REFLECTION_PROBE_SPECULAR;
                        const bool arrayed = binding == GLOBAL_BINDING_REFLECTION_PROBE_SPECULAR || binding == GLOBAL_BINDING_SKIN_PROFILE_LUT_ARRAY;
                        if (image.dim != (cube ? spv::DimCube : spv::Dim2D) || image.arrayed != arrayed || image.ms || image.depth ||
                            compiler.get_type(image.type).basetype != spirv_cross::SPIRType::Float || compiler.get_type(image.type).width != 32) return false;
                    }
                    if (std::find(parsed.globalBindings.begin(),parsed.globalBindings.end(),binding)==parsed.globalBindings.end()) parsed.globalBindings.push_back(binding);
                    const auto& variable = compiler.get_type(resource.type_id);
                    std::uint64_t count = 1;
                    for (auto length : variable.array) { if (!length) { count = declared->descriptorCount; break; } count *= length; }
                    return count <= declared->descriptorCount && (type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                        compiler.get_buffer_block_flags(resource.id).get(spv::DecorationNonWritable));
                }
                const auto& variableType = compiler.get_type(resource.type_id);
                if (!variableType.array.empty()) return false; // Pass descriptor arrays require their own declared contract.
                if (set == DS_PASS)
                {
                    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    {
                        const auto texture = std::find_if(definition.textures.begin(), definition.textures.end(),
                            [&](const auto& t) { return t.binding == binding; });
                        const auto& image = variableType.image;
                        return texture != definition.textures.end() && image.dim == spv::Dim2D &&
                            !image.arrayed && !image.ms && !image.depth &&
                            compiler.get_type(image.type).basetype == spirv_cross::SPIRType::Float &&
                            compiler.get_type(image.type).width == 32;
                    }
                    if (binding == 0) return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
                        compiler.get_declared_struct_size(compiler.get_type(resource.base_type_id)) <= parameterBytes;
                    const auto buffer = std::find_if(definition.buffers.begin(), definition.buffers.end(), [&](const auto& value) { return value.binding == binding; });
                    if (buffer == definition.buffers.end()) return false;
                    const auto expected = buffer->role == "uniformInput" ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    if (type != expected) return false;
                    const auto& block = compiler.get_type(resource.base_type_id);
                    if (type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && !compiler.get_buffer_block_flags(resource.id).get(spv::DecorationNonWritable)) return false;
                    if (compiler.get_declared_struct_size(block) > std::uint64_t(buffer->stride) * buffer->maxElements) return false;
                    if (buffer->role == "storageInput" && !block.member_types.empty())
                    {
                        const auto last = static_cast<std::uint32_t>(block.member_types.size() - 1);
                        const auto& member = compiler.get_type(block.member_types[last]);
                        if (!member.array.empty() && member.array.back() == 0 &&
                            compiler.type_struct_member_array_stride(block, last) != buffer->stride) return false;
                    }
                    if (buffer->role == "instanceInput")
                    {
                        if (block.member_types.size() != 1 || compiler.type_struct_member_offset(block, 0) != 0 ||
                            compiler.type_struct_member_array_stride(block, 0) != sizeof(VansProjectDrawInstance)) return false;
                        const auto& records = compiler.get_type(block.member_types[0]);
                        if (records.array.empty() || records.array.back() != 0) return false;
                        const auto& instance = compiler.get_type(records.self);
                        if (instance.member_types.size() != 3 || compiler.type_struct_member_offset(instance, 0) != 0 ||
                            compiler.type_struct_member_offset(instance, 1) != 64 || compiler.type_struct_member_offset(instance, 2) != 80) return false;
                        for (std::size_t index = 0; index < 3; ++index)
                        {
                            const auto& member = compiler.get_type(instance.member_types[index]);
                            if (member.basetype != spirv_cross::SPIRType::Float || member.width != 32 || member.vecsize != 4 ||
                                member.columns != (index == 0 ? 4 : 1)) return false;
                        }
                    }
                    return true;
                }
                if (definition.source != Vans::VansRenderPassSource::Scene) return false;
                if (set == DS_OBJECT) return type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && binding <= 1 && compiler.get_buffer_block_flags(resource.id).get(spv::DecorationNonWritable);
                if (set == DS_VERTEX_DEFORMATION) return type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && binding <= 3 && compiler.get_buffer_block_flags(resource.id).get(spv::DecorationNonWritable);
                if (set == 4)
                {
                    const auto& image = variableType.image;
                    return type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && binding <= 5 &&
                        image.dim == spv::Dim2D && !image.arrayed && !image.ms && !image.depth &&
                        compiler.get_type(image.type).basetype == spirv_cross::SPIRType::Float && compiler.get_type(image.type).width == 32;
                }
                return false;
            };
            for (const auto& resource : resources.uniform_buffers) if (!validateResource(resource, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)) return fail("Uniform descriptor is incompatible with the Pass ABI");
            for (const auto& resource : resources.storage_buffers) if (!validateResource(resource, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)) return fail("Storage descriptor is incompatible with the Pass ABI or is writable");
            for (const auto& resource : resources.sampled_images) if (!validateResource(resource, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)) return fail("Sampled descriptor is unavailable before built-in producers");
            if (stage == VK_SHADER_STAGE_FRAGMENT_BIT)
            {
                std::unordered_set<std::uint32_t> outputs;
                for (const auto& output : resources.stage_outputs)
                {
                    const auto location = compiler.get_decoration(output.id, spv::DecorationLocation);
                    const auto& type = compiler.get_type(output.type_id);
                    if (location >= colors || type.basetype != spirv_cross::SPIRType::Float || type.width != 32 ||
                        !type.array.empty() || type.columns != 1 || !outputs.insert(location).second) return fail("Fragment output signature does not match declared color targets");
                }
                if (outputs.size() != colors) return fail("Every declared color target must have exactly one fragment output");
            }
            if (stage == VK_SHADER_STAGE_VERTEX_BIT)
                for (const auto& input : resources.stage_inputs)
                {
                    const auto& type=compiler.get_type(input.type_id);
                    if (!type.array.empty() || type.columns!=1 || type.width!=32 || type.vecsize<1 || type.vecsize>4)
                        return fail("Vertex inputs require scalar/vector 32-bit attributes");
                    const VkFormat floats[]={VK_FORMAT_R32_SFLOAT,VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32G32B32_SFLOAT,VK_FORMAT_R32G32B32A32_SFLOAT};
                    const VkFormat ints[]={VK_FORMAT_R32_SINT,VK_FORMAT_R32G32_SINT,VK_FORMAT_R32G32B32_SINT,VK_FORMAT_R32G32B32A32_SINT};
                    const VkFormat uints[]={VK_FORMAT_R32_UINT,VK_FORMAT_R32G32_UINT,VK_FORMAT_R32G32B32_UINT,VK_FORMAT_R32G32B32A32_UINT};
                    const auto* formats=type.basetype==spirv_cross::SPIRType::Float?floats:type.basetype==spirv_cross::SPIRType::Int?ints:type.basetype==spirv_cross::SPIRType::UInt?uints:nullptr;
                    if (!formats) return fail("Unsupported vertex attribute numeric type");
                    parsed.vertexInputs.push_back({compiler.get_decoration(input.id,spv::DecorationLocation),formats[type.vecsize-1]});
                }
        }
    }
    catch (const std::exception& exception) { error = definition.passId + ": SPIR-V reflection failed: " + exception.what(); return false; }
    signature = std::move(parsed); error.clear(); return true;
}
bool ValidateProjectPassShader(const Vans::VansRenderPassAsset& definition,
    const VansGraphicsShader& shader, std::uint32_t parameterBytes,
    VansProjectPassShaderSignature& signature, std::string& error)
{
    const auto& state = shader.GetPipelineProgramDesc().graphicsState;
    if (!ValidateGraphicsAttachmentBlends(state, error)) return false;
    const auto colors = std::count_if(definition.targets.begin(), definition.targets.end(), [](const auto& target) { return target.format != Vans::VansRenderTargetFormat::D32; });
    const bool depth = std::any_of(definition.targets.begin(), definition.targets.end(), [](const auto& target) { return target.format == Vans::VansRenderTargetFormat::D32; });
    if (shader.GetPushConstantSize() != 0 || (state.colorAttachmentCount >= 0 ? state.colorAttachmentCount != colors : colors != 1) ||
        (!depth && (state.depthTest || state.depthWrite))) { error = definition.passId + ": Shader depth, output count or push constants are incompatible"; return false; }
    if (state.colorAttachmentCount > 0 && (state.enableAlphaBlend || state.enablePremultipliedAlphaBlend || state.enableDecalBlend))
    {
        error = definition.passId + ": explicit attachment count disables this Shader blend mode; use automatic attachment count for a single color target";
        return false;
    }
    std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>> stages;
    for (const auto& [stage, module] : shader.m_ShaderModuleDataMap)
    {
        if (module.m_ShaderSPIRVCode.size() % 4) { error = "Invalid SPIR-V byte size"; return false; }
        auto& words = stages[stage]; words.resize(module.m_ShaderSPIRVCode.size() / 4);
        if (!words.empty()) std::memcpy(words.data(), module.m_ShaderSPIRVCode.data(), module.m_ShaderSPIRVCode.size());
    }
    if (!ValidateProjectPassShaderSpirv(definition, stages, parameterBytes, signature, error)) return false;
    signature.binaryHash = shader.GetShaderBinaryHash();
    return true;
}
}
