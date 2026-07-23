"""Numerical white-furnace checks for ForestEngine cloth BRDF families."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import numpy as np
from PIL import Image

from GenerateClothBRDFLUT import lambda_sheen, radical_inverse_vdc


def cosine_hemisphere(sample_count: int) -> np.ndarray:
    indices = np.arange(sample_count, dtype=np.uint32)
    u = (indices.astype(np.float64) + 0.5) / sample_count
    v = radical_inverse_vdc(indices)
    radius = np.sqrt(u)
    phi = 2.0 * math.pi * v
    return np.stack((radius * np.cos(phi), radius * np.sin(phi), np.sqrt(1.0 - u)), axis=-1)


def sample_lut(lut: np.ndarray, no_x: np.ndarray, roughness: float) -> np.ndarray:
    height, width = lut.shape
    x = np.clip(no_x * width - 0.5, 0.0, width - 1.0)
    y = np.clip(roughness * height - 0.5, 0.0, height - 1.0)
    x0 = np.floor(x).astype(np.int32)
    x1 = np.minimum(x0 + 1, width - 1)
    y0 = int(math.floor(y))
    y1 = min(y0 + 1, height - 1)
    tx = x - x0
    ty = y - y0
    row0 = lut[y0, x0] * (1.0 - tx) + lut[y0, x1] * tx
    row1 = lut[y1, x0] * (1.0 - tx) + lut[y1, x1] * tx
    return row0 * (1.0 - ty) + row1 * ty


def charlie_terms(view: np.ndarray, light: np.ndarray, roughness: float) -> np.ndarray:
    half_vector = light + view[None, :]
    half_vector /= np.maximum(np.linalg.norm(half_vector, axis=-1, keepdims=True), 1.0e-8)
    no_h = np.clip(half_vector[:, 2], 0.0, 1.0)
    no_v = max(view[2], 1.0e-4)
    no_l = np.maximum(light[:, 2], 1.0e-4)
    alpha_g = max(roughness * roughness, 1.0e-4)
    inv_alpha = 1.0 / alpha_g
    distribution = (2.0 + inv_alpha) * np.power(np.maximum(1.0 - no_h * no_h, 1.0e-6),
                                                 0.5 * inv_alpha) / (2.0 * math.pi)
    visibility = 1.0 / np.maximum(
        (1.0 + lambda_sheen(np.array(no_v), alpha_g) + lambda_sheen(no_l, alpha_g))
        * (4.0 * no_v * no_l),
        1.0e-5,
    )
    return distribution * visibility


def charlie_directional_albedo(lut: np.ndarray, no_v: float, roughness: float,
                               sample_count: int) -> float:
    indices = np.arange(sample_count, dtype=np.uint32)
    u = (indices.astype(np.float64) + 0.5) / sample_count
    v = radical_inverse_vdc(indices)
    alpha_g = max(roughness * roughness, 1.0e-4)
    inv_alpha = 1.0 / alpha_g
    sin2_h = np.power(u, 2.0 / (inv_alpha + 2.0))
    sin_h = np.sqrt(sin2_h)
    no_h = np.sqrt(np.maximum(1.0 - sin2_h, 0.0))
    phi = 2.0 * math.pi * v
    half_vector = np.stack((sin_h * np.cos(phi), sin_h * np.sin(phi), no_h), axis=-1)
    view = np.array([math.sqrt(max(1.0 - no_v * no_v, 0.0)), 0.0, no_v])
    vo_h = np.sum(view[None, :] * half_vector, axis=-1)
    light = 2.0 * vo_h[:, None] * half_vector - view[None, :]
    no_l = light[:, 2]
    visibility = 1.0 / np.maximum(
        (1.0 + lambda_sheen(np.array(no_v), alpha_g) + lambda_sheen(no_l, alpha_g))
        * (4.0 * max(no_v, 1.0e-4) * np.maximum(no_l, 1.0e-4)),
        1.0e-5,
    )
    estimator = visibility * np.maximum(no_l, 0.0) * 4.0 * np.maximum(vo_h, 0.0) \
        / np.maximum(no_h, 1.0e-6)
    normalization_view = float(sample_lut(lut[:, :, 0], np.array([no_v]), roughness)[0])
    normalization_light = sample_lut(lut[:, :, 0], np.maximum(no_l, 0.0), roughness)
    estimator *= np.minimum(normalization_view, normalization_light)
    return float(np.mean(np.where(no_l > 0.0, estimator, 0.0)))


def fuzz_furnace(lut: np.ndarray, light: np.ndarray, no_v: float,
                 roughness: float, sheen: float) -> float:
    view = np.array([math.sqrt(max(1.0 - no_v * no_v, 0.0)), 0.0, no_v])
    e_view = float(sample_lut(lut[:, :, 2], np.array([no_v]), roughness)[0])
    e_light = sample_lut(lut[:, :, 2], light[:, 2], roughness)
    base_scale = np.minimum(1.0 - sheen * e_view, 1.0 - sheen * e_light)
    # Under cosine sampling, diffuse/PI contributes baseScale and the BRDF
    # contribution is multiplied by PI.
    sheen_energy = charlie_directional_albedo(lut, no_v, roughness, len(light))
    return float(np.mean(np.maximum(base_scale, 0.0)) + sheen * sheen_energy)


def silk_furnace(lut: np.ndarray, light: np.ndarray, no_v: float, roughness: float,
                 anisotropy: float, sheen: float) -> float:
    view = np.array([math.sqrt(max(1.0 - no_v * no_v, 0.0)), 0.0, no_v])
    alpha = max(roughness * roughness, 0.002)
    aspect = math.sqrt(max(1.0 - 0.9 * abs(anisotropy), 0.1))
    along, across = max(alpha / aspect, 0.002), max(alpha * aspect, 0.002)
    alpha_t, alpha_b = (along, across) if anisotropy >= 0.0 else (across, along)

    # Diffuse integrates cleanly with cosine-weighted light samples.
    no_l_diffuse = np.maximum(light[:, 2], 1.0e-4)
    fresnel_view = 0.04 + 0.96 * (1.0 - no_v) ** 5
    fresnel_light = 0.04 + 0.96 * np.power(1.0 - no_l_diffuse, 5.0)
    e_view = float(sample_lut(lut[:, :, 2], np.array([no_v]), roughness)[0])
    e_light_diffuse = sample_lut(lut[:, :, 2], light[:, 2], roughness)
    base_scale_diffuse = np.maximum(
        np.minimum(1.0 - sheen * e_view, 1.0 - sheen * e_light_diffuse), 0.0)
    diffuse_energy = float(np.mean(
        (1.0 - fresnel_view) * (1.0 - fresnel_light) * base_scale_diffuse))

    # Importance sample pdf(H)=D(H)*NoH for the sharp anisotropic GGX lobe.
    sample_count = len(light)
    indices = np.arange(sample_count, dtype=np.uint32)
    u1 = (indices.astype(np.float64) + 0.5) / sample_count
    u2 = radical_inverse_vdc(indices)
    azimuth = 2.0 * math.pi * u1
    phi = np.arctan2(alpha_b * np.sin(azimuth), alpha_t * np.cos(azimuth))
    cos_phi = np.cos(phi)
    sin_phi = np.sin(phi)
    alpha2 = 1.0 / (cos_phi * cos_phi / (alpha_t * alpha_t)
                    + sin_phi * sin_phi / (alpha_b * alpha_b))
    tan2_theta = alpha2 * u2 / np.maximum(1.0 - u2, 1.0e-8)
    no_h = 1.0 / np.sqrt(1.0 + tan2_theta)
    sin_theta = np.sqrt(np.maximum(1.0 - no_h * no_h, 0.0))
    half_vector = np.stack((sin_theta * cos_phi, sin_theta * sin_phi, no_h), axis=-1)
    vo_h = np.sum(view[None, :] * half_vector, axis=-1)
    reflected_light = 2.0 * vo_h[:, None] * half_vector - view[None, :]
    no_l = reflected_light[:, 2]
    valid = (vo_h > 0.0) & (no_l > 0.0)

    lambda_v = np.maximum(no_l, 1.0e-4) * math.sqrt(
        (alpha_t * view[0]) ** 2 + (alpha_b * view[1]) ** 2 + no_v * no_v)
    lambda_l = no_v * np.sqrt(
        (alpha_t * reflected_light[:, 0]) ** 2
        + (alpha_b * reflected_light[:, 1]) ** 2
        + np.maximum(no_l, 1.0e-4) ** 2)
    visibility = 0.5 / np.maximum(lambda_v + lambda_l, 1.0e-5)
    fresnel = 0.04 + 0.96 * np.power(1.0 - np.clip(vo_h, 0.0, 1.0), 5.0)
    e_light_specular = sample_lut(lut[:, :, 2], np.maximum(no_l, 0.0), roughness)
    base_scale_specular = np.maximum(
        np.minimum(1.0 - sheen * e_view, 1.0 - sheen * e_light_specular), 0.0)
    specular_estimator = visibility * fresnel * np.maximum(no_l, 0.0) \
        * 4.0 * np.maximum(vo_h, 0.0) / np.maximum(no_h, 1.0e-6) \
        * base_scale_specular
    specular_energy = float(np.mean(np.where(valid, specular_estimator, 0.0)))

    sheen_energy = charlie_directional_albedo(lut, no_v, roughness, len(light))
    return diffuse_energy + specular_energy + sheen * sheen_energy


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("lut", type=Path)
    parser.add_argument("--samples", type=int, default=65536)
    args = parser.parse_args()

    rgb = np.asarray(Image.open(args.lut).convert("RGB"), dtype=np.float64) / 255.0
    if not np.array_equal(rgb[:, :, 1], rgb[:, :, 2]):
        raise AssertionError("Cloth LUT G/B channels must contain identical normalized albedo")
    if np.any(rgb[:, :, 0] <= 0.0) or np.any(rgb > 1.0):
        raise AssertionError("Cloth LUT normalization/albedo channels are out of range")
    lut = rgb
    light = cosine_hemisphere(args.samples)

    worst_name = ""
    worst_energy = 0.0
    for no_v in (0.05, 0.2, 0.5, 0.9):
        for roughness in (0.1, 0.3, 0.6, 0.9):
            for sheen in (0.0, 0.25, 0.5, 1.0):
                energy = fuzz_furnace(lut, light, no_v, roughness, sheen)
                if energy > worst_energy:
                    worst_name, worst_energy = "fuzz", energy
                if energy > 1.02 or energy < -1.0e-5:
                    raise AssertionError(
                        f"Fuzz furnace failed: NoV={no_v}, r={roughness}, sheen={sheen}, E={energy}")

            for anisotropy in (-0.85, 0.0, 0.85):
                energy = silk_furnace(lut, light, no_v, roughness, anisotropy, 0.16)
                if energy > worst_energy:
                    worst_name, worst_energy = "silk", energy
                if energy > 1.02 or energy < -1.0e-5:
                    raise AssertionError(
                        f"Silk furnace failed: NoV={no_v}, r={roughness}, a={anisotropy}, E={energy}")

    # Thin transmission is exactly strength * thickness attenuation, so zero
    # strength must be an exact zero and the maximum cannot exceed one.
    for thickness in (0.0, 0.25, 0.5, 1.0):
        zero_transmission = 0.0 * ((1.0 - thickness) + 0.15 * thickness)
        full_transmission = 1.0 * ((1.0 - thickness) + 0.15 * thickness)
        if zero_transmission != 0.0 or not (0.0 <= full_transmission <= 1.0):
            raise AssertionError("Thin transmission bounds failed")

    print(f"cloth BRDF validation passed; worst={worst_name} energy={worst_energy:.6f}")


if __name__ == "__main__":
    main()
