#include "VansPcgSplineEvaluator.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace Vans
{
namespace
{
glm::vec3 Vector(const std::array<float, 3>& v) { return { v[0], v[1], v[2] }; }
std::array<float, 3> Array(glm::vec3 v) { return { v.x, v.y, v.z }; }
glm::vec3 Unit(glm::vec3 v, glm::vec3 otherwise)
{
    const float length = glm::length(v);
    return length > 1e-7f ? v / length : otherwise;
}
glm::vec3 Position(const VansPcgSplinePoint& a, const VansPcgSplinePoint& b, float t)
{
    const auto p = Vector(a.position), q = Vector(b.position);
    if (a.outgoing == VansPcgSplineSegmentMode::Line) return glm::mix(p, q, t);
    const float r = 1 - t;
    return r*r*r*p + 3*r*r*t*(p + Vector(a.leave)) + 3*r*t*t*(q + Vector(b.arrive)) + t*t*t*q;
}
glm::vec3 Derivative(const VansPcgSplinePoint& a, const VansPcgSplinePoint& b, float t)
{
    const auto p = Vector(a.position), q = Vector(b.position);
    if (a.outgoing == VansPcgSplineSegmentMode::Line) return q - p;
    const float r = 1 - t;
    return 3*r*r*Vector(a.leave) + 6*r*t*(q + Vector(b.arrive) - p - Vector(a.leave)) - 3*t*t*Vector(b.arrive);
}
void Interpolate(const VansPcgSplinePoint& a, const VansPcgSplinePoint& b, float t, VansPcgSplineSample& result)
{
    result.leftWidth = glm::mix(a.leftWidth, b.leftWidth, t);
    result.rightWidth = glm::mix(a.rightWidth, b.rightWidth, t);
    result.bankAngleDegrees = glm::mix(a.bankAngleDegrees, b.bankAngleDegrees, t);
    result.depth = glm::mix(a.depth, b.depth, t);
    result.bankSteepness = glm::mix(a.bankSteepness,b.bankSteepness,t);
    result.speed = glm::mix(a.speed, b.speed, t);
}
}

float VansPcgSplineEvaluator::SmoothWeight(float value)
{
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t*t*t*(t*(t*6 - 15) + 10);
}

void VansPcgSplineEvaluator::ResolveAutoTangents(VansPcgSpline& spline)
{
    for (std::size_t i = 0; i < spline.points.size(); ++i)
    {
        auto& p = spline.points[i];
        if (p.tangentMode != VansPcgSplineTangentMode::Auto || spline.points.size() < 2) continue;
        const auto at = Vector(p.position);
        const auto before = Vector(spline.points[i > 0 ? i - 1 : i].position);
        const auto after = Vector(spline.points[i + 1 < spline.points.size() ? i + 1 : i].position);
        const auto direction = Unit(after - before, Unit(after - at, {1, 0, 0}));
        p.arrive = Array(-direction * glm::length(at - before) / 3.0f);
        p.leave = Array(direction * glm::length(after - at) / 3.0f);
    }
}

bool VansPcgSplineEvaluator::Evaluate(const VansPcgSpline& source, float spacing, float tolerance,
    VansPcgEvaluatedSpline& result, std::string& error)
{
    error.clear();
    result = {};
    result.id = source.id;
    if (source.points.size() < 2) { error = "Spline needs at least two points."; return false; }
    if (!(spacing > 0) || !(tolerance > 0)) { error = "Invalid spline sampling tolerance."; return false; }
    auto spline = source;
    ResolveAutoTangents(spline);
    result.pointDistances.resize(spline.points.size());
    result.minimum = glm::vec2(std::numeric_limits<float>::max());
    result.maximum = -result.minimum;
    const auto append = [&](std::size_t segment, float t) {
        const auto& a = spline.points[segment]; const auto& b = spline.points[segment + 1];
        VansPcgSplineSample sample;
        sample.position = Position(a, b, t);
        sample.tangent = Unit(Derivative(a, b, t), Unit(Vector(b.position) - Vector(a.position), {1, 0, 0}));
        sample.right = Unit(glm::cross(sample.tangent, glm::vec3(0, 1, 0)), {0, 0, 1});
        sample.segment = segment; sample.parameter = t;
        if (!result.samples.empty()) result.length += glm::distance(sample.position, result.samples.back().position);
        sample.distance = result.length;
        result.samples.push_back(sample);
    };
    append(0, 0);
    for (std::size_t segment = 0; segment + 1 < spline.points.size(); ++segment)
    {
        const auto& a = spline.points[segment]; const auto& b = spline.points[segment + 1];
        result.pointDistances[segment] = result.length;
        std::function<bool(float, float, unsigned)> subdivide;
        subdivide = [&](float t0, float t1, unsigned level) {
            const auto p0 = Position(a, b, t0), p1 = Position(a, b, t1);
            const float tm = (t0 + t1) * 0.5f;
            const float tq0 = glm::mix(t0, t1, .25f), tq1 = glm::mix(t0, t1, .75f);
            const float deviation = std::max({glm::distance(Position(a,b,tm), (p0+p1)*.5f),
                glm::distance(Position(a,b,tq0), glm::mix(p0,p1,.25f)),
                glm::distance(Position(a,b,tq1), glm::mix(p0,p1,.75f))});
            if (glm::distance(p0, p1) > spacing || deviation > tolerance)
            {
                if (level >= 20 || result.samples.size() >= 262144) { error = "Spline sampling budget exceeded."; return false; }
                return subdivide(t0, tm, level + 1) && subdivide(tm, t1, level + 1);
            }
            append(segment, t1); return true;
        };
        if (!subdivide(0, 1, 0)) return false;
        result.pointDistances[segment + 1] = result.length;
        if (result.pointDistances[segment + 1] - result.pointDistances[segment] < 0.001f)
        { error = "Spline contains a zero-length segment."; return false; }
    }
    for (auto& sample : result.samples)
    {
        const auto segment = sample.segment;
        const float localLength = result.pointDistances[segment + 1] - result.pointDistances[segment];
        const float factor = (sample.distance - result.pointDistances[segment]) / localLength;
        Interpolate(spline.points[segment], spline.points[segment + 1], factor, sample);
        if (glm::length(glm::vec2(sample.tangent.x, sample.tangent.z)) < .01f)
        { error = "Vertical spline sections cannot be represented by a height field."; return false; }
        const float extent = std::max(sample.leftWidth, sample.rightWidth) + spline.shoulder + spacing;
        const glm::vec2 xz(sample.position.x, sample.position.z);
        result.minimum = glm::min(result.minimum, xz - extent);
        result.maximum = glm::max(result.maximum, xz + extent);
    }
    if (source.continuation && source.envelopeOffset + result.length > source.envelopeLength + tolerance)
    { error = "Continuation lies outside its flow envelope."; return false; }
    return true;
}

float VansPcgSplineEvaluator::EndpointFade(const VansPcgSpline& spline, float distance, float length)
{
    const float envelopeLength = spline.continuation ? spline.envelopeLength : length;
    const float along = (spline.continuation ? spline.envelopeOffset : 0) + distance;
    const float sigma = spline.flowSign > 0 ? along : envelopeLength - along;
    const float start = spline.fadeInDistance > 0 ? SmoothWeight(sigma / spline.fadeInDistance) : 1;
    const float end = spline.fadeOutDistance > 0 ? SmoothWeight((envelopeLength - sigma) / spline.fadeOutDistance) : 1;
    return start * end;
}

float VansPcgSplineEvaluator::WaterEndpointWeight(const VansPcgSpline& spline, float distance, float length, float minimumWidth)
{
    const float envelopeLength = spline.continuation ? spline.envelopeLength : length;
    const float along = (spline.continuation ? spline.envelopeOffset : 0) + distance;
    // 字段保留线性过渡坐标，CPU/GPU 重建时统一 smoothstep 一次。
    // 双重平滑会把衰减挤到过渡带中段，使进入坡面后仍长时间接近满强度。
    const float start = spline.waterBlendStartMeters > 0 ? std::clamp(along / std::max(spline.waterBlendStartMeters, minimumWidth),0.f,1.f) : 1;
    const float end = spline.waterBlendEndMeters > 0 ? std::clamp((envelopeLength - along) / std::max(spline.waterBlendEndMeters, minimumWidth),0.f,1.f) : 1;
    return std::clamp(start * end,0.f,1.f);
}

glm::vec2 VansPcgSplineEvaluator::Velocity(const VansPcgSpline& spline, const VansPcgSplineSample& sample, float length)
{
    return glm::vec2(sample.tangent.x, sample.tangent.z) *
        (sample.speed * static_cast<float>(spline.flowSign) * EndpointFade(spline, sample.distance, length));
}

bool VansPcgSplineEvaluator::InsertPoint(VansPcgSpline& spline, std::size_t segment, float t,
    const std::string& pointId, std::string& error)
{
    if (segment + 1 >= spline.points.size() || !(t > 0 && t < 1) || pointId.empty())
    { error = "Select an interior position on a spline segment."; return false; }
    if (std::any_of(spline.points.begin(), spline.points.end(), [&](const auto& p){return p.id == pointId;}))
    { error = "Point ID already exists."; return false; }
    VansPcgEvaluatedSpline evaluated;
    if (!Evaluate(spline, .1f, .001f, evaluated, error)) return false;
    ResolveAutoTangents(spline);
    // 固化全部 Auto 手柄，保证插点不因邻接关系改变而改变其他段。
    for (auto& point : spline.points)
        if (point.tangentMode == VansPcgSplineTangentMode::Auto) point.tangentMode = VansPcgSplineTangentMode::Broken;
    auto& a = spline.points[segment]; auto& b = spline.points[segment + 1];
    VansPcgSplinePoint inserted = a; inserted.id = pointId;
    const auto p0 = Vector(a.position), p1 = p0 + Vector(a.leave);
    const auto p3 = Vector(b.position), p2 = p3 + Vector(b.arrive);
    const auto ab = glm::mix(p0,p1,t), bc = glm::mix(p1,p2,t), cd = glm::mix(p2,p3,t);
    const auto abc = glm::mix(ab,bc,t), bcd = glm::mix(bc,cd,t), at = glm::mix(abc,bcd,t);
    if (a.outgoing == VansPcgSplineSegmentMode::Line) inserted.position = Array(glm::mix(p0,p3,t));
    else
    {
        inserted.position = Array(at); inserted.arrive = Array(abc-at); inserted.leave = Array(bcd-at);
        a.leave = Array(ab-p0); b.arrive = Array(cd-p3);
        a.tangentMode = b.tangentMode = VansPcgSplineTangentMode::Broken;
    }
    float along = evaluated.pointDistances[segment];
    float previousT = 0, previousDistance = along;
    for (const auto& sample : evaluated.samples)
    {
        if (sample.segment != segment) continue;
        if (sample.parameter >= t)
        {
            const float f = (t - previousT) / std::max(sample.parameter - previousT, 1e-7f);
            along = glm::mix(previousDistance, sample.distance, f); break;
        }
        previousT = sample.parameter; previousDistance = sample.distance;
    }
    VansPcgSplineSample properties;
    Interpolate(a,b,(along-evaluated.pointDistances[segment]) /
        (evaluated.pointDistances[segment+1]-evaluated.pointDistances[segment]),properties);
    inserted.leftWidth=properties.leftWidth; inserted.rightWidth=properties.rightWidth;
    inserted.linkedWidth=a.linkedWidth && b.linkedWidth;
    inserted.depth=properties.depth; inserted.bankSteepness=properties.bankSteepness; inserted.speed=properties.speed; inserted.bankAngleDegrees=properties.bankAngleDegrees;
    inserted.tangentMode=VansPcgSplineTangentMode::Broken;
    spline.points.insert(spline.points.begin()+segment+1,std::move(inserted));
    return true;
}

void VansPcgSplineEvaluator::ReversePointOrder(VansPcgSpline& spline, float length)
{
    ResolveAutoTangents(spline);
    std::vector<VansPcgSplineSegmentMode> segments;
    for (const auto& p : spline.points) segments.push_back(p.outgoing);
    std::reverse(spline.points.begin(),spline.points.end());
    for (std::size_t i=0; i<spline.points.size(); ++i)
    {
        auto& p=spline.points[i]; std::swap(p.arrive,p.leave); std::swap(p.leftWidth,p.rightWidth);
        p.bankAngleDegrees=-p.bankAngleDegrees;
        if (i+1<spline.points.size()) p.outgoing=segments[spline.points.size()-2-i];
    }
    spline.flowSign=-spline.flowSign;
    std::swap(spline.waterBlendStartMeters,spline.waterBlendEndMeters);
    spline.coordinateOffset += spline.coordinateSign*length;
    spline.coordinateSign=-spline.coordinateSign;
    if (spline.continuation) spline.envelopeOffset=spline.envelopeLength-spline.envelopeOffset-length;
}
}
