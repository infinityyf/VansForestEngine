#include "VansRenderPassAsset.h"
#include "VansAssetReference.h"
#include "Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <unordered_set>
#include <utility>

namespace Vans
{
namespace
{
using Value = VansSerializedValue;
bool Fail(std::string& error, const std::string& field, const char* reason)
{
    error = field + ": " + reason;
    return false;
}
bool Object(const Value& value, std::initializer_list<const char*> allowed,
    const std::string& path, std::string& error)
{
    if (value.kind != Value::Kind::Object) return Fail(error, path, "expected object");
    std::unordered_set<std::string> fields;
    for (const auto& [key, unused] : value.objectFields)
    {
        if (!fields.insert(key).second) return Fail(error, path + "." + key, "duplicate field");
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* field) { return key == field; }))
            return Fail(error, path + "." + key, "unknown field");
    }
    return true;
}
const Value* Field(const Value& value, const char* name) { return FindObjectField(value, name); }
bool Text(const Value& object, const char* name, std::string& output,
    const std::string& path, std::string& error, bool required = true)
{
    const auto* field = Field(object, name);
    if (!field && !required) return true;
    if (!field || field->kind != Value::Kind::String || field->stringValue.empty() ||
        field->stringValue.size() > 256 || field->stringValue.find('\0')!=std::string::npos)
        return Fail(error, path + "." + name, "expected nonempty string (at most 256 bytes)");
    output = field->stringValue;
    return true;
}
bool Integer(const Value& object, const char* name, std::int64_t& output,
    std::int64_t minimum, std::int64_t maximum, const std::string& path, std::string& error,
    bool required = true)
{
    const auto* field = Field(object, name);
    if (!field && !required) return true;
    if (!field || field->kind != Value::Kind::Int || field->intValue < minimum || field->intValue > maximum)
        return Fail(error, path + "." + name, "integer outside supported range");
    output = field->intValue;
    return true;
}
bool Boolean(const Value& object, const char* name, bool& output,
    const std::string& path, std::string& error)
{
    const auto* field = Field(object, name);
    if (!field) return true;
    if (field->kind != Value::Kind::Bool) return Fail(error, path + "." + name, "expected boolean");
    output = field->boolValue;
    return true;
}
bool Numbers(const Value& value, std::vector<float>& output, const std::string& path, std::string& error)
{
    if (value.kind != Value::Kind::Array || value.arrayItems.empty() || value.arrayItems.size() > 64)
        return Fail(error, path, "expected 1..64 finite numbers");
    for (const auto& number : value.arrayItems)
    {
        if (number.kind != Value::Kind::Int && number.kind != Value::Kind::Float)
            return Fail(error, path, "expected number");
        const double n = number.kind == Value::Kind::Int ? double(number.intValue) : number.floatValue;
        if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max())
            return Fail(error, path, "nonfinite or overflowing number");
        output.push_back(static_cast<float>(n));
    }
    return true;
}
bool ReadTarget(const Value& root, bool depth, VansRenderTargetAsset& target,
    const std::string& path, std::string& error)
{
    if (!Object(root, {"name", "format", "size", "usage", "lifetime", "load", "store",
        "initialClear", "exportChannel", "exportSlot", "samples"}, path, error)) return false;
    std::string format, lifetime, load, store;
    if (!Text(root, "name", target.name, path, error) || !Text(root, "format", format, path, error) ||
        !Text(root, "lifetime", lifetime, path, error) || !Text(root, "load", load, path, error) ||
        !Text(root, "store", store, path, error) ||
        !Text(root, "exportChannel", target.exportChannel, path, error, false)) return false;
    if (format == "r8_unorm") target.format = VansRenderTargetFormat::R8;
    else if (format == "rgba8_unorm") target.format = VansRenderTargetFormat::RGBA8;
    else if (format == "rgba16_sfloat") target.format = VansRenderTargetFormat::RGBA16F;
    else if (format == "rgba32_sfloat") target.format = VansRenderTargetFormat::RGBA32F;
    else if (format == "d32_sfloat") target.format = VansRenderTargetFormat::D32;
    else return Fail(error, path + ".format", "unsupported format");
    if (depth != (target.format == VansRenderTargetFormat::D32))
        return Fail(error, path + ".format", "attachment aspect does not match format");
    if (lifetime == "frameLocal") target.lifetime = VansRenderTargetLifetime::FrameLocal;
    else if (lifetime == "persistent") target.lifetime = VansRenderTargetLifetime::Persistent;
    else return Fail(error, path + ".lifetime", "expected frameLocal or persistent");
    if (load == "clear") target.load = VansRenderTargetLoad::Clear;
    else if (load == "load") target.load = VansRenderTargetLoad::Load;
    else if (load == "copyPrevious") target.load = VansRenderTargetLoad::CopyPrevious;
    else return Fail(error, path + ".load", "expected clear, load or copyPrevious");
    if (target.load != VansRenderTargetLoad::Clear && target.lifetime != VansRenderTargetLifetime::Persistent)
        return Fail(error, path + ".load", "retained contents require persistent lifetime");
    if (store != "store") return Fail(error, path + ".store", "project outputs require store");
    std::int64_t samples = 1;
    if (!Integer(root, "samples", samples, 1, 1, path, error, false)) return false;
    const auto* size = Field(root, "size");
    if (!size) return Fail(error, path + ".size", "required field");
    if (!Object(*size, {"mode", "width", "height", "scale"}, path + ".size", error)) return false;
    std::string mode;
    if (!Text(*size, "mode", mode, path + ".size", error)) return false;
    if (mode == "absolute")
    {
        if (Field(*size, "scale")) return Fail(error, path + ".size.scale", "only valid for renderResolution");
        std::int64_t width = 0, height = 0;
        if (!Integer(*size, "width", width, 1, 16384, path + ".size", error) ||
            !Integer(*size, "height", height, 1, 16384, path + ".size", error)) return false;
        target.width = static_cast<std::uint32_t>(width);
        target.height = static_cast<std::uint32_t>(height);
    }
    else if (mode == "renderResolution")
    {
        if (Field(*size, "width") || Field(*size, "height"))
            return Fail(error, path + ".size", "renderResolution uses scale, not dimensions");
        const auto* scale = Field(*size, "scale");
        if (scale)
        {
            std::vector<float> numbers;
            if (!Numbers(Value::Array({*scale}), numbers, path + ".size.scale", error) ||
                numbers[0] <= 0.0f || numbers[0] > 4.0f) return Fail(error, path + ".size.scale", "expected (0,4]");
            target.resolutionScale = numbers[0];
        }
        target.followsRenderResolution = true;
    }
    else return Fail(error, path + ".size.mode", "unsupported size mode");
    const auto* usage = Field(root, "usage");
    if (!usage || usage->kind != Value::Kind::Array || usage->arrayItems.empty())
        return Fail(error, path + ".usage", "expected usage array");
    for (const auto& item : usage->arrayItems)
    {
        if (item.kind != Value::Kind::String) return Fail(error, path + ".usage", "expected string");
        std::uint32_t bit = 0;
        if (item.stringValue == "colorAttachment") bit = RenderTargetColorAttachment;
        else if (item.stringValue == "depthAttachment") bit = RenderTargetDepthAttachment;
        else if (item.stringValue == "sampled") bit = RenderTargetSampled;
        else if (item.stringValue == "transferSrc") bit = RenderTargetTransferSource;
        else if (item.stringValue == "transferDst") bit = RenderTargetTransferDestination;
        else return Fail(error, path + ".usage", "unsupported image usage");
        if (target.usage & bit) return Fail(error, path + ".usage", "duplicate usage");
        target.usage |= bit;
    }
    const auto attachment = depth ? RenderTargetDepthAttachment : RenderTargetColorAttachment;
    const auto other = depth ? RenderTargetColorAttachment : RenderTargetDepthAttachment;
    if (!(target.usage & attachment) || (target.usage & other))
        return Fail(error, path + ".usage", "incorrect attachment usage");
    if (!target.exportChannel.empty())
    {
        std::int64_t slot = 0;
        if (depth || !(target.usage & RenderTargetSampled))
            return Fail(error, path + ".exportChannel", "graphics exports require a sampled color target");
        if (!Integer(root, "exportSlot", slot, 0, VansProjectRenderTextureSlotCount-1, path, error)) return false;
        target.exportSlot = static_cast<std::uint32_t>(slot);
    }
    else if (Field(root, "exportSlot")) return Fail(error, path + ".exportSlot", "requires exportChannel");
    if (target.load == VansRenderTargetLoad::CopyPrevious &&
        (target.usage & (RenderTargetTransferSource | RenderTargetTransferDestination)) !=
            (RenderTargetTransferSource | RenderTargetTransferDestination))
        return Fail(error, path + ".usage", "copyPrevious requires transferSrc and transferDst");
    const auto* clear = Field(root, "initialClear");
    std::vector<float> values;
    if (!clear || !Numbers(*clear, values, path + ".initialClear", error) || values.size() != 4)
        return Fail(error, path + ".initialClear", "expected four finite components");
    std::copy(values.begin(), values.end(), target.clear.begin());
    if (depth && (target.clear[0] < 0 || target.clear[0] > 1))
        return Fail(error, path + ".initialClear", "depth must be in [0,1]");
    return true;
}
}

bool ReadRenderPassAsset(const VansSerializedValue& root, VansRenderPassAsset& asset, std::string& error)
{
    error.clear();
    VansRenderPassAsset parsed;
    if (!Object(root, {"passId", "anchor", "order", "enabled", "executor", "source", "shaders",
        "filter", "targets", "history", "buffers", "textures", "parameters", "cameraWorldRect", "budgetBytes", "maxDraws"}, "renderPass", error)) return false;
    std::string anchor, executor, history = "none";
    std::int64_t order = 0, budget = parsed.budgetBytes, maxDraws = parsed.maxDraws;
    if (!Text(root, "passId", parsed.passId, "renderPass", error) ||
        !Text(root, "anchor", anchor, "renderPass", error) ||
        !Text(root, "executor", executor, "renderPass", error) ||
        !Text(root, "history", history, "renderPass", error, false) ||
        !Text(root, "cameraWorldRect", parsed.cameraWorldRect, "renderPass", error, false) ||
        !Integer(root, "order", order, INT32_MIN, INT32_MAX, "renderPass", error) ||
        !Integer(root, "budgetBytes", budget, 1, 1024ll * 1024ll * 1024ll, "renderPass", error, false) ||
        !Integer(root, "maxDraws", maxDraws, 1, 262144, "renderPass", error, false) ||
        !Boolean(root, "enabled", parsed.enabled, "renderPass", error)) return false;
    if (anchor != "BeforeBuiltInRender") return Fail(error, "renderPass.anchor", "only BeforeBuiltInRender is supported");
    if (executor != "DrawObjects") return Fail(error, "renderPass.executor", "only DrawObjects is supported");
    if (history != "none" && history != "previousFrame") return Fail(error, "renderPass.history", "unsupported history");
    parsed.previousFrameHistory = history == "previousFrame";
    parsed.order = static_cast<std::int32_t>(order);
    parsed.budgetBytes = static_cast<std::uint64_t>(budget);
    parsed.maxDraws = static_cast<std::uint32_t>(maxDraws);
    const auto* source = Field(root, "source");
    std::string kind;
    if (!source) return Fail(error, "renderPass.source", "required field");
    if (!Object(*source, {"kind", "stream", "mesh", "overflow"}, "renderPass.source", error) ||
        !Text(*source, "kind", kind, "renderPass.source", error)) return false;
    if (kind == "scene")
    {
        if (source->objectFields.size() != 1) return Fail(error, "renderPass.source", "scene source only accepts kind");
    }
    else if (kind == "projectInstances")
    {
        parsed.source = VansRenderPassSource::ProjectInstances;
        std::string overflow;
        if (!Text(*source, "stream", parsed.instanceStream, "renderPass.source", error) ||
            !Text(*source, "overflow", overflow, "renderPass.source", error)) return false;
        const auto* mesh = Field(*source, "mesh");
        if (!mesh || !TryReadAssetGuidReference(*mesh, parsed.instanceMesh))
            return Fail(error, "renderPass.source.mesh", "expected model GUID reference");
        if (overflow != "rejectNew") return Fail(error, "renderPass.source.overflow", "only rejectNew is supported");
    }
    else return Fail(error, "renderPass.source.kind", "unsupported object source");
    const auto* shaders = Field(root, "shaders");
    if (!shaders || shaders->kind != Value::Kind::Array || shaders->arrayItems.empty() || shaders->arrayItems.size() > 64)
        return Fail(error, "renderPass.shaders", "expected 1..64 GUID references in priority order");
    for (const auto& reference : shaders->arrayItems)
    {
        VansAssetGuid guid;
        if (!TryReadAssetGuidReference(reference, guid) ||
            std::find(parsed.shaders.begin(), parsed.shaders.end(), guid) != parsed.shaders.end())
            return Fail(error, "renderPass.shaders", "invalid or duplicate shader GUID reference");
        parsed.shaders.push_back(guid);
    }
    if (const auto* filter = Field(root, "filter"))
    {
        if (!Object(*filter, {"renderableKinds", "layers", "cameraVisibleOnly"}, "renderPass.filter", error) ||
            !Boolean(*filter, "cameraVisibleOnly", parsed.cameraVisibleOnly, "renderPass.filter", error)) return false;
        if (const auto* kinds = Field(*filter, "renderableKinds"))
        {
            if (kinds->kind != Value::Kind::Array || kinds->arrayItems.size() != 1 ||
                kinds->arrayItems[0].kind != Value::Kind::String || kinds->arrayItems[0].stringValue != "mesh")
                return Fail(error, "renderPass.filter.renderableKinds", "only static mesh is supported");
        }
        if (const auto* layers = Field(*filter, "layers"))
        {
            if (layers->kind != Value::Kind::Array || layers->arrayItems.size() > 64)
                return Fail(error, "renderPass.filter.layers", "expected at most 64 layer names");
            for (const auto& layer : layers->arrayItems)
            {
                if (layer.kind != Value::Kind::String || layer.stringValue.empty() || layer.stringValue.size()>256 || layer.stringValue.find('\0')!=std::string::npos ||
                    std::find(parsed.layers.begin(), parsed.layers.end(), layer.stringValue) != parsed.layers.end())
                    return Fail(error, "renderPass.filter.layers", "empty or duplicate layer");
                parsed.layers.push_back(layer.stringValue);
            }
        }
    }
    const auto* targets = Field(root, "targets");
    if (!targets) return Fail(error, "renderPass.targets", "required field");
    if (!Object(*targets, {"color", "depth"}, "renderPass.targets", error)) return false;
    if (const auto* colors = Field(*targets, "color"))
    {
        if (colors->kind != Value::Kind::Array || colors->arrayItems.size() > 4)
            return Fail(error, "renderPass.targets.color", "expected up to four color targets");
        for (const auto& color : colors->arrayItems)
        {
            VansRenderTargetAsset target;
            if (!ReadTarget(color, false, target, "renderPass.targets.color", error)) return false;
            parsed.targets.push_back(std::move(target));
        }
    }
    if (const auto* depth = Field(*targets, "depth"))
    {
        VansRenderTargetAsset target;
        if (!ReadTarget(*depth, true, target, "renderPass.targets.depth", error)) return false;
        parsed.targets.push_back(std::move(target));
    }
    if (parsed.targets.empty()) return Fail(error, "renderPass.targets", "at least one attachment required");
    std::unordered_set<std::string> names, channels;
    std::unordered_set<std::uint32_t> slots;
    for (const auto& target : parsed.targets)
    {
        if (!names.insert(target.name).second ||
            (!target.exportChannel.empty() && (!channels.insert(target.exportChannel).second || !slots.insert(target.exportSlot).second)))
            return Fail(error, "renderPass.targets", "duplicate target name, export channel or export slot");
        if (target.load == VansRenderTargetLoad::CopyPrevious && !parsed.previousFrameHistory)
            return Fail(error, "renderPass.history", "copyPrevious requires previousFrame");
        const auto& first = parsed.targets.front();
        if (target.width != first.width || target.height != first.height ||
            target.followsRenderResolution != first.followsRenderResolution || target.resolutionScale != first.resolutionScale)
            return Fail(error, "renderPass.targets", "all attachments must use the same extent policy");
    }
    if (const auto* buffers = Field(root, "buffers"))
    {
        if (buffers->kind != Value::Kind::Array || buffers->arrayItems.size() > 16)
            return Fail(error, "renderPass.buffers", "expected at most 16 buffers");
        std::unordered_set<std::uint32_t> bindings;
        std::unordered_set<std::string> bufferNames;
        for (const auto& value : buffers->arrayItems)
        {
            VansRenderPassBufferAsset buffer;
            std::string update, lifetime;
            if (!Object(value, {"name", "role", "layout", "binding", "stride", "maxElements", "update", "lifetime"},
                "renderPass.buffers", error) || !Text(value, "name", buffer.name, "renderPass.buffers", error) ||
                !Text(value, "role", buffer.role, "renderPass.buffers", error) ||
                !Text(value, "layout", buffer.layout, "renderPass.buffers", error) ||
                !Text(value, "update", update, "renderPass.buffers", error) ||
                !Text(value, "lifetime", lifetime, "renderPass.buffers", error)) return false;
            if (update != "perFrame" || lifetime != "frameLocal")
                return Fail(error, "renderPass.buffers", "input buffers require perFrame/frameLocal");
            std::int64_t binding = 1, stride = 0, count = 0;
            if (buffer.role == "instanceInput" && buffer.layout == "projectDrawInstance") stride = 96;
            if (!Integer(value, "binding", binding, 1, 31, "renderPass.buffers", error, false) ||
                !Integer(value, "stride", stride, 16, 65536, "renderPass.buffers", error, stride == 0) ||
                !Integer(value, "maxElements", count, 1, 1048576, "renderPass.buffers", error)) return false;
            if (stride % 16 != 0 || (buffer.role != "instanceInput" && buffer.role != "uniformInput" && buffer.role != "storageInput"))
                return Fail(error, "renderPass.buffers", "unsupported role or unaligned stride");
            if (buffer.role == "instanceInput" && (buffer.layout != "projectDrawInstance" || stride != 96 ||
                parsed.source != VansRenderPassSource::ProjectInstances || buffer.name != parsed.instanceStream))
                return Fail(error, "renderPass.buffers", "instance input must match source stream and projectDrawInstance ABI");
            buffer.binding = static_cast<std::uint32_t>(binding);
            buffer.stride = static_cast<std::uint32_t>(stride);
            buffer.maxElements = static_cast<std::uint32_t>(count);
            if (!bindings.insert(buffer.binding).second || !bufferNames.insert(buffer.name).second ||
                std::uint64_t(buffer.stride) * buffer.maxElements > parsed.budgetBytes)
                return Fail(error, "renderPass.buffers", "duplicate binding/name or buffer exceeds budget");
            parsed.buffers.push_back(std::move(buffer));
        }
    }
    if (const auto* textures = Field(root, "textures"))
    {
        if (textures->kind != Value::Kind::Array || textures->arrayItems.size() > 16)
            return Fail(error, "renderPass.textures", "expected at most 16 sampled textures");
        std::unordered_set<std::uint32_t> bindings{0};
        for (const auto& buffer : parsed.buffers) bindings.insert(buffer.binding);
        std::unordered_set<std::string> names;
        for (const auto& value : textures->arrayItems)
        {
            VansRenderPassTextureAsset texture;
            std::int64_t binding = 0;
            if (!Object(value, {"name", "asset", "binding"}, "renderPass.textures", error) ||
                !Text(value, "name", texture.name, "renderPass.textures", error) ||
                !Integer(value, "binding", binding, 1, 31, "renderPass.textures", error)) return false;
            const auto* reference = Field(value, "asset");
            if (!reference || !TryReadAssetGuidReference(*reference, texture.asset) || !texture.asset.IsValid())
                return Fail(error, "renderPass.textures.asset", "expected texture GUID reference");
            texture.binding = static_cast<std::uint32_t>(binding);
            if (!bindings.insert(texture.binding).second || !names.insert(texture.name).second)
                return Fail(error, "renderPass.textures", "duplicate name or descriptor binding");
            parsed.textures.push_back(std::move(texture));
        }
    }
    if (parsed.source == VansRenderPassSource::ProjectInstances &&
        std::count_if(parsed.buffers.begin(), parsed.buffers.end(), [](const auto& b) { return b.role == "instanceInput"; }) != 1)
        return Fail(error, "renderPass.buffers", "projectInstances requires exactly one instanceInput");
    if (const auto* parameters = Field(root, "parameters"))
    {
        if (parameters->kind != Value::Kind::Object || parameters->objectFields.size() > 64)
            return Fail(error, "renderPass.parameters", "expected up to 64 named parameters");
        std::unordered_set<std::string> parameterNames;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
        for (const auto& [name, value] : parameters->objectFields)
        {
            VansRenderPassParameterAsset parameter;
            parameter.name = name;
            std::int64_t offset = 0;
            if (name.empty() || !parameterNames.insert(name).second ||
                !Object(value, {"offset", "value"}, "renderPass.parameters." + name, error) ||
                !Integer(value, "offset", offset, 0, 65280, "renderPass.parameters." + name, error))
                return Fail(error, "renderPass.parameters." + name, "invalid declaration");
            const auto* numbers = Field(value, "value");
            if (!numbers) return Fail(error, "renderPass.parameters." + name + ".value", "required field");
            if (!Numbers(*numbers, parameter.initialValue, "renderPass.parameters." + name, error)) return false;
            parameter.offset = static_cast<std::uint32_t>(offset);
            const auto end = parameter.offset + static_cast<std::uint32_t>(parameter.initialValue.size() * sizeof(float));
            if (parameter.offset % 16 != 0 || end > 65536 ||
                std::any_of(ranges.begin(), ranges.end(), [&](const auto& r) { return parameter.offset < r.second && end > r.first; }))
                return Fail(error, "renderPass.parameters." + name, "unaligned or overlapping uniform byte range");
            ranges.emplace_back(parameter.offset, end);
            parsed.parameters.push_back(std::move(parameter));
        }
    }
    if (!parsed.cameraWorldRect.empty())
    {
        const auto parameter = std::find_if(parsed.parameters.begin(), parsed.parameters.end(),
            [&](const auto& p) { return p.name == parsed.cameraWorldRect; });
        if (parameter == parsed.parameters.end() || parameter->initialValue.size() != 4 ||
            parameter->initialValue[2] <= 0 || parameter->initialValue[3] <= 0)
            return Fail(error, "renderPass.cameraWorldRect", "requires a vec4 world rectangle with positive X/Z ranges");
        if (!parsed.previousFrameHistory || std::any_of(parsed.targets.begin(), parsed.targets.end(), [](const auto& target) {
            return target.followsRenderResolution || target.format == VansRenderTargetFormat::D32 ||
                target.load != VansRenderTargetLoad::CopyPrevious;
        })) return Fail(error, "renderPass.cameraWorldRect", "requires fixed-size color targets with copyPrevious history");
    }
    asset = std::move(parsed);
    return true;
}

std::vector<VansAssetGuid> CollectRenderPassAssetDependencies(const VansRenderPassAsset& asset)
{
    std::vector<VansAssetGuid> result = asset.shaders;
    if (asset.instanceMesh.IsValid()) result.push_back(asset.instanceMesh);
    for (const auto& texture : asset.textures) result.push_back(texture.asset);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

VansSerializedValue WriteRenderPassAsset(const VansRenderPassAsset& asset)
{
    const auto guid = [](VansAssetGuid value) { return Value::Object({{"guid", Value::String(value.ToString())}}); };
    const auto floats = [](const auto& numbers) {
        std::vector<Value> items;
        for (const auto n : numbers) items.push_back(Value::Float(n));
        return Value::Array(std::move(items));
    };
    std::vector<Value> shaders, colors, buffers, layers;
    Value depth;
    for (const auto shader : asset.shaders) shaders.push_back(guid(shader));
    for (const auto& layer : asset.layers) layers.push_back(Value::String(layer));
    for (const auto& target : asset.targets)
    {
        const char* format = "rgba8_unorm";
        switch (target.format)
        {
        case VansRenderTargetFormat::R8: format = "r8_unorm"; break;
        case VansRenderTargetFormat::RGBA16F: format = "rgba16_sfloat"; break;
        case VansRenderTargetFormat::RGBA32F: format = "rgba32_sfloat"; break;
        case VansRenderTargetFormat::D32: format = "d32_sfloat"; break;
        default: break;
        }
        std::vector<Value> usages;
        for (const auto& usage : {std::pair<std::uint32_t, const char*>{RenderTargetColorAttachment, "colorAttachment"},
            {RenderTargetDepthAttachment, "depthAttachment"}, {RenderTargetSampled, "sampled"},
            {RenderTargetTransferSource, "transferSrc"}, {RenderTargetTransferDestination, "transferDst"}})
            if (target.usage & usage.first) usages.push_back(Value::String(usage.second));
        Value size = target.followsRenderResolution
            ? Value::Object({{"mode", Value::String("renderResolution")}, {"scale", Value::Float(target.resolutionScale)}})
            : Value::Object({{"mode", Value::String("absolute")}, {"width", Value::Int(target.width)}, {"height", Value::Int(target.height)}});
        auto encoded = Value::Object({
            {"name", Value::String(target.name)}, {"format", Value::String(format)}, {"size", std::move(size)},
            {"usage", Value::Array(std::move(usages))},
            {"lifetime", Value::String(target.lifetime == VansRenderTargetLifetime::Persistent ? "persistent" : "frameLocal")},
            {"load", Value::String(target.load == VansRenderTargetLoad::Clear ? "clear" : target.load == VansRenderTargetLoad::Load ? "load" : "copyPrevious")},
            {"store", Value::String("store")}, {"initialClear", floats(target.clear)}});
        if (!target.exportChannel.empty())
        {
            encoded.objectFields.emplace_back("exportChannel", Value::String(target.exportChannel));
            encoded.objectFields.emplace_back("exportSlot", Value::Int(target.exportSlot));
        }
        if (target.format == VansRenderTargetFormat::D32) depth = std::move(encoded);
        else colors.push_back(std::move(encoded));
    }
    Value targets = Value::Object({{"color", Value::Array(std::move(colors))}});
    if (!depth.IsNull()) targets.objectFields.emplace_back("depth", std::move(depth));
    Value source = Value::Object({{"kind", Value::String(asset.source == VansRenderPassSource::Scene ? "scene" : "projectInstances")}});
    if (asset.source == VansRenderPassSource::ProjectInstances)
    {
        source.objectFields.emplace_back("stream", Value::String(asset.instanceStream));
        source.objectFields.emplace_back("mesh", guid(asset.instanceMesh));
        source.objectFields.emplace_back("overflow", Value::String("rejectNew"));
    }
    for (const auto& buffer : asset.buffers) buffers.push_back(Value::Object({
        {"name", Value::String(buffer.name)}, {"role", Value::String(buffer.role)}, {"layout", Value::String(buffer.layout)},
        {"binding", Value::Int(buffer.binding)}, {"stride", Value::Int(buffer.stride)}, {"maxElements", Value::Int(buffer.maxElements)},
        {"update", Value::String("perFrame")}, {"lifetime", Value::String("frameLocal")}}));
    Value parameters = Value::Object({});
    for (const auto& parameter : asset.parameters) parameters.objectFields.emplace_back(parameter.name,
        Value::Object({{"offset", Value::Int(parameter.offset)}, {"value", floats(parameter.initialValue)}}));
    auto result = Value::Object({{"passId", Value::String(asset.passId)}, {"anchor", Value::String("BeforeBuiltInRender")},
        {"executor", Value::String("DrawObjects")}, {"order", Value::Int(asset.order)}, {"enabled", Value::Bool(asset.enabled)},
        {"source", std::move(source)}, {"shaders", Value::Array(std::move(shaders))},
        {"filter", Value::Object({{"renderableKinds", Value::Array({Value::String("mesh")})},
            {"layers", Value::Array(std::move(layers))}, {"cameraVisibleOnly", Value::Bool(asset.cameraVisibleOnly)}})},
        {"targets", std::move(targets)}, {"history", Value::String(asset.previousFrameHistory ? "previousFrame" : "none")},
        {"budgetBytes", Value::Int(asset.budgetBytes)}, {"maxDraws", Value::Int(asset.maxDraws)},
        {"buffers", Value::Array(std::move(buffers))}, {"parameters", std::move(parameters)}});
    if (!asset.cameraWorldRect.empty()) result.objectFields.emplace_back("cameraWorldRect", Value::String(asset.cameraWorldRect));
    if (!asset.textures.empty())
    {
        std::vector<Value> textures;
        for (const auto& texture : asset.textures) textures.push_back(Value::Object({
            {"name", Value::String(texture.name)}, {"asset", guid(texture.asset)}, {"binding", Value::Int(texture.binding)}}));
        result.objectFields.emplace_back("textures", Value::Array(std::move(textures)));
    }
    return result;
}
}
