// Module 5: Cramér's V effect size with Cohen-style interpretation thresholds.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>

#include "chi_square.hpp"

namespace stats {

struct CramersV {
    bool computable = false;
    double value = 0;
    int dfStar = 0;               // min(rows-1, cols-1)
    double small = 0, medium = 0, large = 0;
    std::string strength = "n/a";  // negligible | small | medium | large
};

inline CramersV cramersV(const ChiResult& chi, long n) {
    CramersV v;
    if (!chi.computable || n <= 0) return v;
    v.dfStar = std::min(chi.effRows - 1, chi.effCols - 1);
    v.value = std::min(1.0, std::sqrt(chi.statistic / (static_cast<double>(n) * v.dfStar)));
    // Cohen (1988) benchmarks for V, which depend on df* = min(r-1, c-1)
    switch (v.dfStar) {
        case 1: v.small = 0.10; v.medium = 0.30; v.large = 0.50; break;
        case 2: v.small = 0.07; v.medium = 0.21; v.large = 0.35; break;
        case 3: v.small = 0.06; v.medium = 0.17; v.large = 0.29; break;
        default: v.small = 0.05; v.medium = 0.15; v.large = 0.25; break;
    }
    v.strength = v.value < v.small ? "negligible" : v.value < v.medium ? "small" : v.value < v.large ? "medium" : "large";
    v.computable = true;
    return v;
}

}  // namespace stats
