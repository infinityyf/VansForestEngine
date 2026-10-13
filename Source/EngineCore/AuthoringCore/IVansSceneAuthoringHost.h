#pragma once

#include "VansEditorObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <string>
#include <functional>
#include <vector>
#include <cstdint>

namespace Vans
{
class VansSceneDocument;
struct VansSceneAuthoringValue
{
    DocumentPropertyPath path;
    VansSerializedValue value;
};
// 在整组作者编辑验证后执行一次外部提交；失败恢复作者状态，不创建 Undo 记录。
using VansSceneAuthoringCommit=std::function<bool(std::string&)>;

struct VansSceneAuthoringResult
{
	bool success = false;
	std::string message;

	explicit operator bool() const { return success; }
};

class IVansSceneAuthoringHost
{
public:
	virtual ~IVansSceneAuthoringHost() = default;
	virtual VansSceneDocument* SceneDocument() const = 0;
	virtual VansSceneAuthoringResult SetSceneValue(
		const DocumentPropertyPath& path,
		VansSerializedValue value) = 0;
    virtual VansSceneAuthoringResult CommitSceneValues(std::vector<VansSceneAuthoringValue> values,
        std::uint64_t expectedState,const VansSceneAuthoringCommit& commit)=0;
};
} // namespace Vans
