#pragma once

#include <filesystem>

bool RunNavigationAIContractTests();
bool RunDustV3NavigationContractTests(const std::filesystem::path& root);

namespace VansGraphics { class VansAnimationNode; }
bool RunDemoHallNavigationContractTests(const std::filesystem::path& root,
    VansGraphics::VansAnimationNode* whisperAnimation = nullptr);
