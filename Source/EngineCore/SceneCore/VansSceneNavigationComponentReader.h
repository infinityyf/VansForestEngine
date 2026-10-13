#pragma once
#include "VansSceneNavigationComponentConfig.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans
{
struct VansSceneObjectBuildConfig;
class VansSceneNavigationComponentReader final
{
public:
    // 只包含判别字段和当前形状的字段，供目录默认值与 Inspector 切换共同使用。
    static VansSerializedValue CreateObstacleShapeData(const std::string& shape);
	static bool Read(const VansSerializedValue& data, VansRuntimeNavigationSurfaceComponent& output, std::string& error);
	static bool Read(const VansSerializedValue& data, VansRuntimeNavigationSourceComponent& output, std::string& error);
	static bool Read(const VansSerializedValue& data, VansRuntimeNavigationModifierComponent& output, std::string& error);
    static bool Read(const VansSerializedValue& data, VansRuntimeNavigationObstacleComponent& output, std::string& error);
    static bool Read(const VansSerializedValue& data, VansRuntimeNavigationLinkComponent& output, std::string& error);
	static bool Read(const VansSerializedValue& data, VansRuntimeNavigationAgentComponent& output, std::string& error);
	static bool ReadEntity(const VansSerializedValue& entity, VansSceneObjectBuildConfig& output, std::string& error);
};
}
