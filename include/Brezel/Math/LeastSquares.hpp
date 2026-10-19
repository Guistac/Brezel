#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace Brezel::Math {

/**
 * @brief Result of a dense linear least-squares solve (see LeastSquares).
 */
template <std::size_t N>
struct LeastSquaresSolution {
    bool ok{false};                 ///< false: fewer rows than unknowns, rank deficient, or non-finite input
    bool usedQr{false};             ///< true when Cholesky refused the normal equations and the QR factor solved
    std::array<double, N> x{};      ///< the unknowns minimizing sum(w_i * (a_i . x - b_i)^2)
    double residualRms{0.0};        ///< sqrt(sum(w_i * r_i^2) / rows) at the minimum
    std::size_t rows{0};            ///< rows accumulated
};

/**
 * @brief Streaming dense linear least squares for a fixed number of unknowns (1 to 8). Header-only, thread-agnostic,
 * no allocation: rows are folded in as they come and never stored.
 *
 * Two factorizations are kept side by side for every row:
 * - the normal equations `A^T W A` and `A^T W b`, solved by Cholesky (fast, the usual path);
 * - an upper-triangular QR factor `R` and `Q^T b`, updated by Givens rotations, used when Cholesky refuses
 *   (a pivot below `1e-10` of its diagonal: the problem is ill-conditioned for the squared system), and which also
 *   gives the exact minimum residual whichever path solved.
 * A rank-deficient problem (a QR diagonal below `1e-9` of the largest) returns `ok == false`.
 *
 * @code
 * Brezel::Math::LeastSquares<2> ls;              // fit y = a + b * t
 * for (auto [t, y] : samples) ls.addRow({1.0, t}, y);
 * auto sol = ls.solve();                          // sol.x[0] = a, sol.x[1] = b
 * @endcode
 */
template <std::size_t N>
class LeastSquares {
public:
    static_assert(N >= 1 && N <= 8, "LeastSquares supports 1 to 8 unknowns");

    void reset() noexcept { *this = LeastSquares{}; }

    /// Adds one equation `a . x = b` with weight `weight` (> 0). Non-finite input marks the problem invalid.
    void addRow(const std::array<double, N>& a, double b, double weight = 1.0) noexcept {
        if (!(weight > 0.0) || !std::isfinite(weight) || !std::isfinite(b)) { invalid_ = true; return; }
        for (double v : a) if (!std::isfinite(v)) { invalid_ = true; return; }
        const double sw = std::sqrt(weight);
        std::array<double, N> row{};
        for (std::size_t i = 0; i < N; ++i) row[i] = a[i] * sw;
        double rhs = b * sw;

        for (std::size_t i = 0; i < N; ++i) {
            for (std::size_t j = 0; j < N; ++j) ata_[i][j] += row[i] * row[j];
            atb_[i] += row[i] * rhs;
        }

        // Givens update of R and Q^T b: rotate the new row into the triangle, column by column.
        for (std::size_t k = 0; k < N; ++k) {
            if (row[k] == 0.0) continue;
            const double r = std::hypot(r_[k][k], row[k]);
            const double c = r_[k][k] / r;
            const double s = row[k] / r;
            for (std::size_t j = k; j < N; ++j) {
                const double rkj = r_[k][j];
                r_[k][j] = c * rkj + s * row[j];
                row[j] = -s * rkj + c * row[j];
            }
            const double qk = qtb_[k];
            qtb_[k] = c * qk + s * rhs;
            rhs = -s * qk + c * rhs;
        }
        residualSq_ += rhs * rhs;   // what no unknown can explain
        ++rows_;
    }

    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_; }

    [[nodiscard]] LeastSquaresSolution<N> solve() const noexcept {
        LeastSquaresSolution<N> out{};
        out.rows = rows_;
        if (invalid_ || rows_ < N) return out;
        out.residualRms = std::sqrt(residualSq_ / static_cast<double>(rows_));

        if (solveCholesky(out.x)) { out.ok = true; return out; }
        if (solveQr(out.x)) { out.ok = true; out.usedQr = true; return out; }
        out.x = {};
        return out;
    }

private:
    bool solveCholesky(std::array<double, N>& x) const noexcept {
        double l[N][N]{};
        for (std::size_t j = 0; j < N; ++j) {
            double d = ata_[j][j];
            for (std::size_t k = 0; k < j; ++k) d -= l[j][k] * l[j][k];
            if (!(d > 1e-10 * ata_[j][j]) || !(d > 0.0)) return false;
            l[j][j] = std::sqrt(d);
            for (std::size_t i = j + 1; i < N; ++i) {
                double v = ata_[i][j];
                for (std::size_t k = 0; k < j; ++k) v -= l[i][k] * l[j][k];
                l[i][j] = v / l[j][j];
            }
        }
        double y[N]{};
        for (std::size_t i = 0; i < N; ++i) {
            double v = atb_[i];
            for (std::size_t k = 0; k < i; ++k) v -= l[i][k] * y[k];
            y[i] = v / l[i][i];
        }
        for (std::size_t ii = N; ii-- > 0;) {
            double v = y[ii];
            for (std::size_t k = ii + 1; k < N; ++k) v -= l[k][ii] * x[k];
            x[ii] = v / l[ii][ii];
        }
        for (double v : x) if (!std::isfinite(v)) return false;
        return true;
    }

    bool solveQr(std::array<double, N>& x) const noexcept {
        double maxDiag = 0.0;
        for (std::size_t k = 0; k < N; ++k) maxDiag = std::fmax(maxDiag, std::fabs(r_[k][k]));
        if (!(maxDiag > 0.0)) return false;
        for (std::size_t ii = N; ii-- > 0;) {
            if (!(std::fabs(r_[ii][ii]) > 1e-9 * maxDiag)) return false;
            double v = qtb_[ii];
            for (std::size_t k = ii + 1; k < N; ++k) v -= r_[ii][k] * x[k];
            x[ii] = v / r_[ii][ii];
        }
        for (double v : x) if (!std::isfinite(v)) return false;
        return true;
    }

    double ata_[N][N]{};
    double atb_[N]{};
    double r_[N][N]{};
    double qtb_[N]{};
    double residualSq_{0.0};
    std::size_t rows_{0};
    bool invalid_{false};
};

} // namespace Brezel::Math
