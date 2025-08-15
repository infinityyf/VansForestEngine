#pragma once
#include <GLFW/glfw3.h>
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
//#define GLM_FORCE_LEFT_HANDED
#include <../../GLM/vec3.hpp>
#include <../../GLM/vec4.hpp>
#include <../../GLM/mat4x4.hpp>
#include <../../GLM/ext/matrix_transform.hpp>
#include <../../GLM/ext/matrix_clip_space.hpp>
#include <../../GLM/ext/scalar_constants.hpp>

template <class integral>
constexpr integral AlignUp(integral x, size_t a) noexcept
{
	return integral((x + (integral(a) - 1)) & ~integral(a - 1));
}

template <class integral>
constexpr integral AlignDown(integral x, size_t a) noexcept
{
	return integral(x & ~integral(a - 1));
}
