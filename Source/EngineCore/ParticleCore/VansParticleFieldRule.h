#pragma once

#include <string>
#include <vector>

namespace VansGraphics
{
struct VansParticleFieldRule
{
    std::string pathPattern;
    std::vector<std::string> choices;
    bool hasLimits = false;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.01;
};
}
