"""Generate the ForestEngine Charlie directional-albedo LUT.

R contains a white-furnace normalization factor ``1 / max(E_raw, 1)``.
G/B contain the normalized directional albedo ``min(E_raw, 1)``.  The runtime
uses the symmetric minimum of the R factors for direct lighting and B for
base-layer/IBL energy, keeping the fitted visibility reciprocal and bounded
despite its grazing-angle overshoot.
Sampling uses a deterministic Hammersley sequence and cosine-weighted light
directions.  The BRDF matches BRDFCloth.glsl: Charlie D plus the fitted
Conty/Kulla visibility published by KHR_materials_sheen.
"""

from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path

import numpy as np
from PIL import Image


def radical_inverse_vdc(bits: np.ndarray) -> np.ndarray:
    bits = ((bits << 16) | (bits >> 16)) & np.uint32(0xFFFFFFFF)
    bits = ((bits & np.uint32(0x55555555)) << 1) | ((bits & np.uint32(0xAAAAAAAA)) >> 1)
    bits = ((bits & np.uint32(0x33333333)) << 2) | ((bits & np.uint32(0xCCCCCCCC)) >> 2)
    bits = ((bits & np.uint32(0x0F0F0F0F)) << 4) | ((bits & np.uint32(0xF0F0F0F0)) >> 4)
    bits = ((bits & np.uint32(0x00FF00FF)) << 8) | ((bits & np.uint32(0xFF00FF00)) >> 8)
    return bits.astype(np.float64) * 2.3283064365386963e-10


def lambda_fit(x: np.ndarray, alpha_g: float) -> np.ndarray:
    one_minus_alpha_sq = (1.0 - alpha_g) ** 2
    a = 21.5473 * (1.0 - one_minus_alpha_sq) + 25.3245 * one_minus_alpha_sq
    b = 3.82987 * (1.0 - one_minus_alpha_sq) + 3.32435 * one_minus_alpha_sq
    c = 0.19823 * (1.0 - one_minus_alpha_sq) + 0.16801 * one_minus_alpha_sq
    d = -1.97760 * (1.0 - one_minus_alpha_sq) - 1.27393 * one_minus_alpha_sq
    e = -4.32054 * (1.0 - one_minus_alpha_sq) - 4.85967 * one_minus_alpha_sq
    safe_x = np.maximum(x, 1.0e-4)
    return a / (1.0 + b * np.power(safe_x, c)) + d * x + e


def lambda_sheen(cos_theta: np.ndarray, alpha_g: float) -> np.ndarray:
    x = np.clip(np.abs(cos_theta), 1.0e-4, 1.0)
    low = np.exp(lambda_fit(x, alpha_g))
    high = np.exp(2.0 * lambda_fit(np.full_like(x, 0.5), alpha_g)
                  - lambda_fit(1.0 - x, alpha_g))
    return np.where(x < 0.5, low, high)


def generate_lut(size: int, sample_count: int) -> np.ndarray:
    sample_index = np.arange(sample_count, dtype=np.uint32)
    u = (sample_index.astype(np.float64) + 0.5) / float(sample_count)
    v = radical_inverse_vdc(sample_index)

    no_v = ((np.arange(size, dtype=np.float64) + 0.5) / float(size))[:, None]
    view = np.stack(
        (np.sqrt(np.maximum(1.0 - no_v[:, 0] ** 2, 0.0)),
         np.zeros(size, dtype=np.float64),
         no_v[:, 0]),
        axis=-1,
    )[:, None, :]

    result = np.zeros((size, size), dtype=np.float64)
    peak_unscaled = 0.0
    for roughness_index in range(size):
        roughness = (roughness_index + 0.5) / float(size)
        alpha_g = max(roughness * roughness, 1.0e-4)
        inv_alpha = 1.0 / alpha_g

        # Importance sample the projected Charlie NDF: pdf(H) = D(H) * NoH.
        # Its sin^2(theta) CDF has a closed-form inverse.
        sin2_h = np.power(u, 2.0 / (inv_alpha + 2.0))
        sin_h = np.sqrt(np.maximum(sin2_h, 0.0))
        no_h_1d = np.sqrt(np.maximum(1.0 - sin2_h, 0.0))
        phi = 2.0 * math.pi * v
        half_vector_1d = np.stack(
            (sin_h * np.cos(phi), sin_h * np.sin(phi), no_h_1d), axis=-1)
        half_vector = half_vector_1d[None, :, :]
        vo_h = np.sum(view * half_vector, axis=-1)
        light = 2.0 * vo_h[:, :, None] * half_vector - view
        no_l = light[:, :, 2]

        visibility_denominator = (
            1.0 + lambda_sheen(no_v, alpha_g) + lambda_sheen(no_l, alpha_g)
        ) * (4.0 * np.maximum(no_v, 1.0e-4) * np.maximum(no_l, 1.0e-4))
        visibility = 1.0 / np.maximum(visibility_denominator, 1.0e-5)

        # D cancels against pdf(L)=D*NoH/(4*VoH). Samples reflected below
        # the surface contribute zero to the directional albedo integral.
        estimator = visibility * np.maximum(no_l, 0.0) * 4.0 * np.maximum(vo_h, 0.0) \
            / np.maximum(no_h_1d[None, :], 1.0e-6)
        estimator = np.where(no_l > 0.0, estimator, 0.0)
        directional_albedo = np.mean(estimator, axis=1)
        peak_unscaled = max(peak_unscaled, float(np.max(directional_albedo)))
        result[roughness_index, :] = np.maximum(directional_albedo, 0.0)

    print(f"unscaled directional-albedo peak={peak_unscaled:.9f}")
    normalization = 1.0 / np.maximum(result, 1.0)
    normalized_albedo = np.minimum(result, 1.0)
    rgb = np.stack((normalization, normalized_albedo, normalized_albedo), axis=2)
    return np.rint(rgb * 255.0).astype(np.uint8)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--size", type=int, default=256)
    parser.add_argument("--samples", type=int, default=4096)
    args = parser.parse_args()

    image = generate_lut(args.size, args.samples)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(image, mode="RGB").save(args.output, optimize=False)
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    print(f"wrote {args.output} ({args.size}x{args.size}, {args.samples} samples, sha256={digest})")


if __name__ == "__main__":
    main()
