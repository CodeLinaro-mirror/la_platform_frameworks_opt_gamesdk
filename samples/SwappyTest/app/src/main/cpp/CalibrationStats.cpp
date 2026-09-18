/*
 * Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CalibrationStats.h"

#include <algorithm>
#include <cmath>

namespace dvfs {

// Student's t critical value for two-tailed 95% confidence interval (alpha=0.05).
// Degrees of freedom df = n - 1 or df = n - 2 for regression.
static double get_student_t_95(int df) {
    static const double t_table[] = {12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306,
                                     2.262,  2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120,
                                     2.110,  2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064,
                                     2.060,  2.056, 2.052, 2.048, 2.045, 2.042};
    if (df <= 0) return 2.0;
    if (df <= 30) return t_table[df - 1];
    if (df <= 40) return 2.021;
    if (df <= 60) return 2.000;
    if (df <= 120) return 1.980;
    return 1.960;
}

// Computes OLS fit: y = intercept + slope * x, plus R^2 and RME95 of slope.
LinearRegressionStats compute_linear_regression(const std::vector<double>& x,
                                                const std::vector<double>& y) {
    LinearRegressionStats stats{};
    const size_t n = x.size();
    if (n < 3 || x.size() != y.size()) {
        return stats;
    }
    stats.n = static_cast<int>(n);

    double sum_x = 0.0, sum_y = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum_x += x[i];
        sum_y += y[i];
    }
    const double mean_x = sum_x / n;
    const double mean_y = sum_y / n;

    double s_xx = 0.0, s_yy = 0.0, s_xy = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double dx = x[i] - mean_x;
        const double dy = y[i] - mean_y;
        s_xx += dx * dx;
        s_yy += dy * dy;
        s_xy += dx * dy;
    }

    if (s_xx <= 1e-12) {
        return stats;
    }

    stats.slope = s_xy / s_xx;
    stats.intercept = mean_y - stats.slope * mean_x;

    if (s_yy > 1e-12) {
        stats.r_squared = (s_xy * s_xy) / (s_xx * s_yy);
    } else {
        stats.r_squared = 1.0;
    }

    const int df = static_cast<int>(n) - 2;
    if (df > 0 && stats.slope > 1e-12) {
        const double s_res = std::max(0.0, s_yy - stats.slope * s_xy);
        const double variance_res = s_res / df;
        stats.slope_std_err = std::sqrt(variance_res / s_xx);
        const double t_crit = get_student_t_95(df);
        stats.rme95_pct = (t_crit * stats.slope_std_err / stats.slope) * 100.0;
    }

    // Mean absolute percentage error of predictions against 2-parameter linear model y_pred =
    // intercept + slope * x
    double sum_err_pct = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double y_pred = stats.intercept + stats.slope * x[i];
        double denom = (y_pred > 1e-6) ? y_pred : std::abs(y[i]);
        if (denom > 1e-12) {
            sum_err_pct += (std::abs(y[i] - y_pred) / denom) * 100.0;
        }
    }
    stats.mean_error_pct = sum_err_pct / n;
    stats.valid = true;
    return stats;
}

// Computes OLS regression and first-half vs. second-half throughput drift strictly
// over the recent sliding window of up to `window_samples` (e.g. 16 samples = 4 full
// 4-level sweeps), filtering isolated single-pass OS/compositor preemption spikes via
// 4-bin median baseline residuals when `win_n >= 8`.
SlidingWindowRegressionStats compute_sliding_window_regression(const std::vector<double>& x,
                                                               const std::vector<double>& y,
                                                               size_t window_samples) {
    SlidingWindowRegressionStats res{};
    if (x.size() != y.size() || x.size() < 4 || window_samples < 4) {
        return res;
    }
    const size_t n = x.size();
    const size_t win_n = std::min(n, window_samples);
    const size_t start = n - win_n;
    res.window_start = start;
    res.window_size = win_n;

    std::vector<double> win_x(x.begin() + start, x.begin() + n);
    std::vector<double> win_y(y.begin() + start, y.begin() + n);

    std::vector<double> clean_x = win_x;
    std::vector<double> clean_y = win_y;
    std::vector<bool> is_inlier(win_n, true);

    if (win_n >= 8) {
        // Build a robust baseline fit across 4 x-sorted quantile bins using bin medians
        std::vector<size_t> idx(win_n);
        for (size_t i = 0; i < win_n; ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return (win_x[a] != win_x[b]) ? (win_x[a] < win_x[b]) : (win_y[a] < win_y[b]);
        });
        std::vector<double> bin_x, bin_y;
        for (size_t b = 0; b < 4; ++b) {
            const size_t b_start = (b * win_n) / 4;
            const size_t b_end = ((b + 1) * win_n) / 4;
            if (b_end <= b_start) continue;
            std::vector<double> bx, by;
            for (size_t k = b_start; k < b_end; ++k) {
                bx.push_back(win_x[idx[k]]);
                by.push_back(win_y[idx[k]]);
            }
            std::sort(bx.begin(), bx.end());
            std::sort(by.begin(), by.end());
            const size_t mid = bx.size() / 2;
            bin_x.push_back(bx[mid]);
            bin_y.push_back(by[mid]);
        }
        LinearRegressionStats base_fit = compute_linear_regression(bin_x, bin_y);
        if (bin_x.size() == 4 &&
            (!base_fit.valid || base_fit.slope <= 1e-12 || base_fit.r_squared < 0.990)) {
            std::vector<double> up_x(bin_x.begin() + 1, bin_x.end());
            std::vector<double> up_y(bin_y.begin() + 1, bin_y.end());
            LinearRegressionStats up_fit = compute_linear_regression(up_x, up_y);
            if (up_fit.valid && up_fit.slope > 1e-12 && up_fit.r_squared >= 0.992 &&
                (!base_fit.valid || base_fit.slope <= 1e-12 ||
                 up_fit.r_squared > base_fit.r_squared + 0.008)) {
                base_fit = up_fit;
            }
        }
        if (!base_fit.valid || base_fit.slope <= 1e-12) {
            base_fit = compute_linear_regression(win_x, win_y);
        }
        if (base_fit.valid && base_fit.slope > 1e-12) {
            std::vector<double> abs_res(win_n);
            for (size_t i = 0; i < win_n; ++i) {
                const double y_pred = base_fit.intercept + base_fit.slope * win_x[i];
                abs_res[i] = std::abs(win_y[i] - y_pred);
            }
            std::vector<double> sorted_res = abs_res;
            std::sort(sorted_res.begin(), sorted_res.end());
            const double med_res = sorted_res[sorted_res.size() / 2];
            const double mad_sigma = 1.4826 * med_res;

            // Identify isolated preemption spikes or floor-clamped shortest-level passes
            std::vector<size_t> outlier_candidates;
            for (size_t i = 0; i < win_n; ++i) {
                const double y_pred = std::max(0.5, base_fit.intercept + base_fit.slope * win_x[i]);
                if (abs_res[i] > std::max(2.5 * mad_sigma, 0.06 * y_pred)) {
                    outlier_candidates.push_back(i);
                }
            }
            std::sort(outlier_candidates.begin(), outlier_candidates.end(),
                      [&](size_t a, size_t b) { return abs_res[a] > abs_res[b]; });
            const size_t max_outliers = std::max<size_t>(1, win_n / 4);
            const size_t drop_count = std::min(outlier_candidates.size(), max_outliers);
            if (drop_count > 0 && win_n - drop_count >= 6) {
                for (size_t k = 0; k < drop_count; ++k) {
                    is_inlier[outlier_candidates[k]] = false;
                }
                clean_x.clear();
                clean_y.clear();
                for (size_t i = 0; i < win_n; ++i) {
                    if (is_inlier[i]) {
                        clean_x.push_back(win_x[i]);
                        clean_y.push_back(win_y[i]);
                    }
                }
            }
        }
    }

    res.ols = compute_linear_regression(clean_x, clean_y);

    // Split into balanced first and second halves of the inlier window
    const size_t clean_n = clean_x.size();
    const size_t half_len = clean_n / 2;
    if (half_len >= 3) {
        std::vector<double> h1_x(clean_x.begin(), clean_x.begin() + half_len);
        std::vector<double> h1_y(clean_y.begin(), clean_y.begin() + half_len);
        std::vector<double> h2_x(clean_x.end() - half_len, clean_x.end());
        std::vector<double> h2_y(clean_y.end() - half_len, clean_y.end());

        LinearRegressionStats h1 = compute_linear_regression(h1_x, h1_y);
        LinearRegressionStats h2 = compute_linear_regression(h2_x, h2_y);
        if (h1.valid && h2.valid && h1.slope > 1e-12 && h2.slope > 1e-12) {
            const double rate_half1 = 1.0 / h1.slope;
            const double rate_half2 = 1.0 / h2.slope;
            res.plateau_drift_pct =
                    (std::abs(rate_half2 - rate_half1) / std::max(rate_half2, 1e-9)) * 100.0;
        }
    }
    return res;
}

} // namespace dvfs
