#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace Brezel::Math {

/**
 * @brief Total least-squares line through 2D points (see fitLine2D / LineFitAccumulator).
 */
struct LineFit2D {
    bool ok{false};               ///< false with fewer than 2 points, all points equal, or non-finite input
    double centroid[2]{};         ///< a point on the line (the centroid)
    double direction[2]{1.0, 0.0};///< unit direction; the sign is arbitrary (orient it with a displacement)
    double rms{0.0};              ///< RMS perpendicular distance of the points to the line
    double extent{0.0};           ///< sqrt of the variance along the line (spread of the points along it)
    std::size_t count{0};
};

/**
 * @brief Streaming total least squares 2D line: running mean and co-moments (Welford), no allocation, no storage.
 * The direction is the principal axis of the point covariance; the RMS is the square root of its small eigenvalue.
 */
class LineFitAccumulator {
public:
    void reset() noexcept { *this = LineFitAccumulator{}; }

    void add(double x, double y) noexcept {
        if (!std::isfinite(x) || !std::isfinite(y)) { invalid_ = true; return; }
        ++n_;
        const double dx = x - mx_;
        const double dy = y - my_;
        mx_ += dx / static_cast<double>(n_);
        my_ += dy / static_cast<double>(n_);
        sxx_ += dx * (x - mx_);
        syy_ += dy * (y - my_);
        sxy_ += dx * (y - my_);
    }

    [[nodiscard]] std::size_t count() const noexcept { return n_; }

    [[nodiscard]] LineFit2D fit() const noexcept {
        LineFit2D out{};
        out.count = n_;
        if (invalid_ || n_ < 2) return out;
        const double inv = 1.0 / static_cast<double>(n_);
        const double cxx = sxx_ * inv, cyy = syy_ * inv, cxy = sxy_ * inv;
        const double half = 0.5 * (cxx - cyy);
        const double root = std::sqrt(half * half + cxy * cxy);
        const double lMax = 0.5 * (cxx + cyy) + root;
        const double lMin = std::fmax(0.0, 0.5 * (cxx + cyy) - root);
        if (!(lMax > 0.0)) return out;
        const double angle = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);
        out.ok = true;
        out.centroid[0] = mx_;
        out.centroid[1] = my_;
        out.direction[0] = std::cos(angle);
        out.direction[1] = std::sin(angle);
        out.rms = std::sqrt(lMin);
        out.extent = std::sqrt(lMax);
        return out;
    }

private:
    std::size_t n_{0};
    double mx_{0.0}, my_{0.0};
    double sxx_{0.0}, syy_{0.0}, sxy_{0.0};
    bool invalid_{false};
};

/// Fits a line to `n` points given by `pointAt(i)`, which returns something indexable as [0] = x, [1] = y.
template <typename PointAt>
[[nodiscard]] LineFit2D fitLine2D(std::size_t n, PointAt&& pointAt) noexcept {
    LineFitAccumulator acc;
    for (std::size_t i = 0; i < n; ++i) {
        const auto p = pointAt(i);
        acc.add(static_cast<double>(p[0]), static_cast<double>(p[1]));
    }
    return acc.fit();
}

/// Fits a line to `n` points stored as separate x and y arrays.
[[nodiscard]] inline LineFit2D fitLine2D(const double* x, const double* y, std::size_t n) noexcept {
    return fitLine2D(n, [&](std::size_t i) { return std::array<double, 2>{x[i], y[i]}; });
}

} // namespace Brezel::Math
