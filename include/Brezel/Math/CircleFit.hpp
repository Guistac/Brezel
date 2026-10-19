#pragma once

#include "Brezel/Math/LeastSquares.hpp"
#include <array>
#include <cmath>
#include <cstddef>

namespace Brezel::Math {

/**
 * @brief Algebraic (Kasa) circle fit through 2D points (see fitCircleKasa).
 */
struct CircleFit2D {
    bool ok{false};           ///< false with fewer than 3 points, collinear points, or non-finite input
    double centre[2]{};
    double radius{0.0};
    double rms{0.0};          ///< RMS of (distance to centre - radius) over the points
    std::size_t count{0};
};

/**
 * @brief Kasa circle fit: minimizes sum((x^2 + y^2 + D x + E y + F)^2) as a linear least-squares problem in D, E, F
 * (LeastSquares<3>), on coordinates shifted to the first point for conditioning. Two passes over the points, no
 * allocation. Biased towards smaller radii on short, noisy arcs: use it as a cross-check, not as the only estimate.
 * `pointAt(i)` returns something indexable as [0] = x, [1] = y.
 */
template <typename PointAt>
[[nodiscard]] CircleFit2D fitCircleKasa(std::size_t n, PointAt&& pointAt) noexcept {
    CircleFit2D out{};
    out.count = n;
    if (n < 3) return out;
    const auto p0 = pointAt(0);
    const double ox = static_cast<double>(p0[0]);
    const double oy = static_cast<double>(p0[1]);
    LeastSquares<3> ls;
    for (std::size_t i = 0; i < n; ++i) {
        const auto p = pointAt(i);
        const double x = static_cast<double>(p[0]) - ox;
        const double y = static_cast<double>(p[1]) - oy;
        ls.addRow({x, y, 1.0}, -(x * x + y * y));
    }
    const auto sol = ls.solve();
    if (!sol.ok) return out;
    const double cx = -0.5 * sol.x[0];
    const double cy = -0.5 * sol.x[1];
    const double r2 = cx * cx + cy * cy - sol.x[2];
    if (!(r2 > 0.0) || !std::isfinite(r2)) return out;
    const double r = std::sqrt(r2);
    double sq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto p = pointAt(i);
        const double d = std::hypot(static_cast<double>(p[0]) - ox - cx, static_cast<double>(p[1]) - oy - cy) - r;
        sq += d * d;
    }
    out.ok = true;
    out.centre[0] = cx + ox;
    out.centre[1] = cy + oy;
    out.radius = r;
    out.rms = std::sqrt(sq / static_cast<double>(n));
    return out;
}

/// Kasa fit of `n` points stored as separate x and y arrays.
[[nodiscard]] inline CircleFit2D fitCircleKasa(const double* x, const double* y, std::size_t n) noexcept {
    return fitCircleKasa(n, [&](std::size_t i) { return std::array<double, 2>{x[i], y[i]}; });
}

} // namespace Brezel::Math
