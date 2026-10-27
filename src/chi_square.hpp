// Module 4: Chi-Square Test of Independence, p-value (regularised incomplete gamma),
// expected-frequency assumption checks.
#pragma once
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "contingency.hpp"

namespace stats {

// ---- regularised upper incomplete gamma Q(a, x) (series + continued fraction) ----
inline double gammaPSeries(double a, double x) {
    double ap = a, sum = 1.0 / a, del = sum;
    for (int n = 0; n < 2000; ++n) {
        ap += 1.0;
        del *= x / ap;
        sum += del;
        if (std::fabs(del) < std::fabs(sum) * 1e-16) break;
    }
    return sum * std::exp(-x + a * std::log(x) - std::lgamma(a));
}
inline double gammaQContFrac(double a, double x) {
    const double tiny = 1e-300;
    double b = x + 1.0 - a, c = 1.0 / tiny, d = 1.0 / b, h = d;
    for (int i = 1; i < 2000; ++i) {
        double an = -i * (i - a);
        b += 2.0;
        d = an * d + b; if (std::fabs(d) < tiny) d = tiny;
        c = b + an / c; if (std::fabs(c) < tiny) c = tiny;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < 1e-15) break;
    }
    return std::exp(-x + a * std::log(x) - std::lgamma(a)) * h;
}
inline double gammaQ(double a, double x) {
    if (x < 0 || a <= 0) return std::numeric_limits<double>::quiet_NaN();
    if (x == 0) return 1.0;
    if (x < a + 1.0) return 1.0 - gammaPSeries(a, x);
    return gammaQContFrac(a, x);
}
// P(X >= x) for X ~ chi-square(df)
inline double chiSquareSurvival(double x, int df) { return gammaQ(df / 2.0, x / 2.0); }

struct Note {
    std::string text;
    std::string status;  // "pass" | "fail" | "info"
};

struct ChiResult {
    bool computable = false;
    std::string reason;
    double alpha = 0.05;
    double statistic = 0;
    int df = 0;
    double pValue = std::numeric_limits<double>::quiet_NaN();
    bool significant = false;
    int effRows = 0, effCols = 0;  // non-empty rows/columns actually used
    int cells = 0, cellsBelow5 = 0;
    double pctBelow5 = 0;
    double minExpected = std::numeric_limits<double>::quiet_NaN();
    bool assumptionsMet = false;
    std::vector<Note> notes;
    double resid[K][K] = {};    // standardised (Pearson) residuals (O-E)/sqrt(E)
    double contrib[K][K] = {};  // each cell's contribution to chi-square
};

inline std::string fmt1(double v) {
    char b[32];
    snprintf(b, sizeof b, "%.1f", v);
    return b;
}
inline std::string fmt2(double v) {
    char b[32];
    snprintf(b, sizeof b, "%.2f", v);
    return b;
}

inline ChiResult chiSquareTest(const Contingency& t, double alpha = 0.05) {
    ChiResult r;
    r.alpha = alpha;
    for (int i = 0; i < K; ++i) if (t.rowTot[i] > 0) ++r.effRows;
    for (int j = 0; j < K; ++j) if (t.colTot[j] > 0) ++r.effCols;
    if (r.effRows < 2 || r.effCols < 2) {
        r.reason = "The test needs at least two non-empty categories in both variables (found " +
                   std::to_string(r.effRows) + " exercise and " + std::to_string(r.effCols) +
                   " fitness categories with data).";
        return r;
    }
    double chi = 0;
    r.minExpected = std::numeric_limits<double>::infinity();
    for (int i = 0; i < K; ++i) {
        if (t.rowTot[i] == 0) continue;
        for (int j = 0; j < K; ++j) {
            if (t.colTot[j] == 0) continue;
            double e = t.exp[i][j];
            double diff = t.obs[i][j] - e;
            r.contrib[i][j] = diff * diff / e;
            r.resid[i][j] = diff / std::sqrt(e);
            chi += r.contrib[i][j];
            ++r.cells;
            if (e < 5.0) ++r.cellsBelow5;
            if (e < r.minExpected) r.minExpected = e;
        }
    }
    r.statistic = chi;
    r.df = (r.effRows - 1) * (r.effCols - 1);
    r.pValue = chiSquareSurvival(chi, r.df);
    r.significant = r.pValue < alpha;
    r.pctBelow5 = 100.0 * r.cellsBelow5 / r.cells;
    const bool pctOk = r.pctBelow5 <= 20.0, minOk = r.minExpected >= 1.0;
    r.assumptionsMet = pctOk && minOk;
    r.computable = true;

    r.notes.push_back({"Both variables are categorical and each participant falls in exactly one cell (checked from the data).", "pass"});
    r.notes.push_back({"Observations are assumed to be independent (one record per participant). This depends on how the data were collected and cannot be verified from the file.", "info"});
    r.notes.push_back({std::to_string(r.cellsBelow5) + " of " + std::to_string(r.cells) + " cells (" + fmt1(r.pctBelow5) +
                           "%) have an expected frequency below 5. Rule of thumb: no more than 20%.",
                       pctOk ? "pass" : "fail"});
    r.notes.push_back({"Smallest expected frequency is " + fmt2(r.minExpected) + ". Rule of thumb: every cell should be at least 1.",
                       minOk ? "pass" : "fail"});
    if (r.effRows < K || r.effCols < K)
        r.notes.push_back({"Empty categories were left out of the test, so degrees of freedom are based on " +
                               std::to_string(r.effRows) + " x " + std::to_string(r.effCols) + " categories.",
                           "info"});
    return r;
}

}  // namespace stats
