// Module 3: contingency table (observed, expected, row/column/cell percentages).
#pragma once
#include <vector>

#include "dataset.hpp"

namespace stats {

using ds::K;

struct Contingency {
    long obs[K][K] = {};      // [exercise][fitness]
    long rowTot[K] = {};
    long colTot[K] = {};
    long n = 0;
    double exp[K][K] = {};    // expected under independence
    double rowPct[K][K] = {}; // % of each exercise group
    double colPct[K][K] = {}; // % of each fitness group
    double cellPct[K][K] = {};// % of all participants
};

inline Contingency buildContingency(const std::vector<ds::Record>& recs) {
    Contingency t;
    for (const auto& r : recs) {
        ++t.obs[r.ex][r.fit];
        ++t.rowTot[r.ex];
        ++t.colTot[r.fit];
        ++t.n;
    }
    for (int i = 0; i < K; ++i)
        for (int j = 0; j < K; ++j) {
            t.exp[i][j] = t.n ? static_cast<double>(t.rowTot[i]) * t.colTot[j] / t.n : 0.0;
            t.rowPct[i][j] = t.rowTot[i] ? 100.0 * t.obs[i][j] / t.rowTot[i] : 0.0;
            t.colPct[i][j] = t.colTot[j] ? 100.0 * t.obs[i][j] / t.colTot[j] : 0.0;
            t.cellPct[i][j] = t.n ? 100.0 * t.obs[i][j] / t.n : 0.0;
        }
    return t;
}

}  // namespace stats
