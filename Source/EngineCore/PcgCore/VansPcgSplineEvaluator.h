#pragma once
#include "VansPcgSplineAsset.h"
#include <glm/glm.hpp>

namespace Vans
{
struct VansPcgSplineSample
{
    glm::vec3 position{};
    glm::vec3 tangent{1, 0, 0};
    glm::vec3 right{0, 0, 1};
    float distance = 0;
    float leftWidth = 0;
    float rightWidth = 0;
    float bankAngleDegrees = 0;
    float depth = 0;
    float bankSteepness = 0;
    float speed = 0;
    std::size_t segment = 0;
    float parameter = 0;
};

struct VansPcgEvaluatedSpline
{
    std::string id;
    std::vector<VansPcgSplineSample> samples;
    std::vector<float> pointDistances;
    glm::vec2 minimum{};
    glm::vec2 maximum{};
    float length = 0;
};

class VansPcgSplineEvaluator
{
public:
    static void ResolveAutoTangents(VansPcgSpline& spline);
    static bool Evaluate(const VansPcgSpline& spline, float spacing, float tolerance,
        VansPcgEvaluatedSpline& result, std::string& error);
    static float EndpointFade(const VansPcgSpline& spline, float distance, float length);
    static glm::vec2 Velocity(const VansPcgSpline& spline, const VansPcgSplineSample& sample, float length);
    static bool InsertPoint(VansPcgSpline& spline, std::size_t segment, float parameter,
        const std::string& pointId, std::string& error);
    static void ReversePointOrder(VansPcgSpline& spline, float length);
    static float SmoothWeight(float value);
};
}
