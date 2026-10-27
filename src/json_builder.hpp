// Module 6: JSON responses for the frontend (+ plain-language interpretation text).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "cramers_v.hpp"

namespace api {

using ds::K;

// ---------------- minimal streaming JSON writer ----------------
class JsonWriter {
public:
    JsonWriter& beginObject() { sep(); out_ += '{'; first_.push_back(true); return *this; }
    JsonWriter& endObject() { out_ += '}'; first_.pop_back(); return *this; }
    JsonWriter& beginArray() { sep(); out_ += '['; first_.push_back(true); return *this; }
    JsonWriter& endArray() { out_ += ']'; first_.pop_back(); return *this; }
    JsonWriter& key(const std::string& k) { sep(); quote(k); out_ += ':'; afterKey_ = true; return *this; }
    JsonWriter& value(const std::string& s) { sep(); quote(s); return *this; }
    JsonWriter& value(const char* s) { return value(std::string(s)); }
    JsonWriter& value(bool b) { sep(); out_ += b ? "true" : "false"; return *this; }
    JsonWriter& value(double d) {
        sep();
        if (!std::isfinite(d)) { out_ += "null"; return *this; }
        char b[40];
        snprintf(b, sizeof b, "%.12g", d);
        out_ += b;
        return *this;
    }
    template <class T>
    typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, JsonWriter&>::type
    value(T v) { sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& null() { sep(); out_ += "null"; return *this; }
    template <class T> JsonWriter& field(const std::string& k, const T& v) { key(k); return value(v); }
    const std::string& str() const { return out_; }

    static std::string escape(const std::string& s) {
        std::string o;
        for (unsigned char c : s) {
            switch (c) {
                case '"': o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                default:
                    if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                    else o += static_cast<char>(c);
            }
        }
        return o;
    }

private:
    void sep() {
        if (afterKey_) { afterKey_ = false; return; }
        if (!first_.empty()) { if (!first_.back()) out_ += ','; first_.back() = false; }
    }
    void quote(const std::string& s) { out_ += '"'; out_ += escape(s); out_ += '"'; }
    std::string out_;
    std::vector<bool> first_;
    bool afterKey_ = false;
};

// ---------------- analysis bundle ----------------
struct Analysis {
    ds::Freq ex, fit;
    stats::Contingency tab;
    stats::ChiResult chi;
    stats::CramersV cv;
};

inline Analysis analyze(const ds::Dataset& d, double alpha = 0.05) {
    Analysis a;
    a.ex = ds::frequencies(d.records, true);
    a.fit = ds::frequencies(d.records, false);
    a.tab = stats::buildContingency(d.records);
    a.chi = stats::chiSquareTest(a.tab, alpha);
    a.cv = stats::cramersV(a.chi, a.tab.n);
    return a;
}

inline std::string pDisplay(double p) {
    if (!std::isfinite(p)) return "n/a";
    if (p < 0.0001) return "< 0.0001";
    char b[24];
    snprintf(b, sizeof b, "%.4f", p);
    return b;
}

namespace detail {
template <class F>
void matrix(JsonWriter& w, const std::string& name, F cell) {
    w.key(name).beginArray();
    for (int i = 0; i < K; ++i) {
        w.beginArray();
        for (int j = 0; j < K; ++j) w.value(cell(i, j));
        w.endArray();
    }
    w.endArray();
}
template <class T>
void vec(JsonWriter& w, const std::string& name, const T& v) {
    w.key(name).beginArray();
    for (auto x : v) w.value(x);
    w.endArray();
}
inline void freq(JsonWriter& w, const std::string& name, const ds::Freq& f) {
    w.key(name).beginObject();
    vec(w, "counts", f.counts);
    vec(w, "percents", f.percents);
    w.field("total", f.total).field("modeIndex", f.mode);
    w.endObject();
}
}  // namespace detail

inline std::string toJson(const ds::Dataset& d, const Analysis& a) {
    const auto& exL = ds::exerciseLabels();
    const auto& fitL = ds::fitnessLabels();
    const auto& c = a.chi;
    JsonWriter w;
    w.beginObject();

    // ---- dataset metadata ----
    w.key("dataset").beginObject();
    w.field("name", d.name).field("synthetic", d.synthetic).field("bundled", d.bundled).field("citation", d.citation);
    w.field("totalRows", d.totalRows).field("validRows", d.records.size());
    w.field("missingRows", d.missingRows).field("invalidRows", d.invalidRows);
    w.key("issues").beginArray();
    for (const auto& i : d.issues) w.beginObject().field("line", i.line).field("message", i.message).endObject();
    w.endArray();
    w.key("warnings").beginArray();
    for (const auto& s : d.warnings) w.value(s);
    w.endArray();
    w.endObject();

    w.key("categories").beginObject();
    w.key("exercise").beginArray(); for (const auto& s : exL) w.value(s); w.endArray();
    w.key("fitness").beginArray(); for (const auto& s : fitL) w.value(s); w.endArray();
    w.endObject();

    // ---- frequency distributions ----
    detail::freq(w, "exercise", a.ex);
    detail::freq(w, "fitness", a.fit);
    w.key("summary").beginObject();
    w.field("n", a.tab.n);
    w.field("topExercise", exL[a.ex.mode]).field("topExerciseCount", a.ex.counts[a.ex.mode]).field("topExercisePct", a.ex.percents[a.ex.mode]);
    w.field("topFitness", fitL[a.fit.mode]).field("topFitnessCount", a.fit.counts[a.fit.mode]).field("topFitnessPct", a.fit.percents[a.fit.mode]);
    w.endObject();

    // ---- contingency table ----
    w.key("contingency").beginObject();
    detail::matrix(w, "observed", [&](int i, int j) { return a.tab.obs[i][j]; });
    detail::matrix(w, "expected", [&](int i, int j) { return a.tab.exp[i][j]; });
    detail::matrix(w, "rowPercents", [&](int i, int j) { return a.tab.rowPct[i][j]; });
    detail::matrix(w, "colPercents", [&](int i, int j) { return a.tab.colPct[i][j]; });
    detail::matrix(w, "cellPercents", [&](int i, int j) { return a.tab.cellPct[i][j]; });
    detail::matrix(w, "residuals", [&](int i, int j) { return c.resid[i][j]; });
    detail::matrix(w, "contributions", [&](int i, int j) { return c.contrib[i][j]; });
    detail::vec(w, "rowTotals", a.tab.rowTot);
    detail::vec(w, "colTotals", a.tab.colTot);
    w.endObject();

    // ---- hypothesis test ----
    w.key("test").beginObject();
    w.field("computable", c.computable).field("reason", c.reason).field("alpha", c.alpha);
    w.field("statistic", c.statistic).field("df", c.df).field("pValue", c.pValue).field("pDisplay", pDisplay(c.pValue));
    w.field("significant", c.significant);
    w.field("decision", !c.computable ? "Test not applicable" : c.significant ? "Reject H0" : "Fail to reject H0");
    w.key("hypotheses").beginObject();
    w.field("null", "H0: Exercise frequency and self-reported fitness are independent (no association in the population).");
    w.field("alternative", "H1: Exercise frequency and self-reported fitness are associated (not independent).");
    w.endObject();
    w.field("effectiveRows", c.effRows).field("effectiveCols", c.effCols);
    w.field("cells", c.cells).field("cellsBelow5", c.cellsBelow5).field("pctCellsBelow5", c.pctBelow5);
    w.field("minExpected", c.minExpected).field("assumptionsMet", c.assumptionsMet);
    w.key("assumptions").beginArray();
    for (const auto& n : c.notes) w.beginObject().field("text", n.text).field("status", n.status).endObject();
    w.endArray();
    w.endObject();

    // ---- Cramér's V ----
    w.key("cramersV").beginObject();
    w.field("computable", a.cv.computable).field("value", a.cv.value).field("dfStar", a.cv.dfStar);
    w.field("strength", a.cv.strength);
    w.key("thresholds").beginArray().value(a.cv.small).value(a.cv.medium).value(a.cv.large).endArray();
    w.endObject();

    // ---- where the association comes from: largest standardised residuals ----
    struct Cell { int i, j; double r; };
    std::vector<Cell> cells;
    if (c.computable)
        for (int i = 0; i < K; ++i)
            for (int j = 0; j < K; ++j)
                if (a.tab.rowTot[i] > 0 && a.tab.colTot[j] > 0) cells.push_back({i, j, c.resid[i][j]});
    std::sort(cells.begin(), cells.end(), [](const Cell& x, const Cell& y) { return std::fabs(x.r) > std::fabs(y.r); });
    w.key("topResiduals").beginArray();
    for (size_t k = 0; k < cells.size() && k < 4; ++k) {
        const Cell& x = cells[k];
        w.beginObject();
        w.field("exercise", exL[x.i]).field("fitness", fitL[x.j]);
        w.field("observed", a.tab.obs[x.i][x.j]).field("expected", a.tab.exp[x.i][x.j]).field("residual", x.r);
        w.field("direction", x.r > 0 ? "more" : "fewer");
        w.endObject();
    }
    w.endArray();

    // ---- plain-language interpretation ----
    std::string headline, plain, caution, causation, synth;
    if (!c.computable) {
        headline = "The Chi-Square test cannot be run on this data";
        plain = c.reason;
    } else {
        char b[64];
        if (c.significant) {
            headline = "Statistically significant association (p " + std::string(a.chi.pValue < 0.0001 ? "< 0.0001" : "= " + pDisplay(c.pValue)) + ")";
            snprintf(b, sizeof b, "%.3f", a.cv.value);
            plain = "Because p is smaller than the 0.05 significance level, we reject the null hypothesis. The data provide sufficient evidence "
                    "that exercise frequency and self-reported fitness are associated: the fitness ratings people give differ across exercise groups "
                    "by more than chance alone would explain. Cramér's V = " + std::string(b) + " indicates a " + a.cv.strength +
                    " association, so statistical significance should not be confused with a large effect.";
        } else {
            headline = "No statistically significant association (p = " + pDisplay(c.pValue) + ")";
            plain = "Because p is not smaller than the 0.05 significance level, we fail to reject the null hypothesis. This sample does not provide "
                    "sufficient evidence of an association between exercise frequency and self-reported fitness. This is not proof that the "
                    "variables are independent; a small sample or a weak relationship can also lead to a non-significant result.";
        }
        if (!c.assumptionsMet)
            caution = "Caution: the expected-frequency assumption is not satisfied (" + std::to_string(c.cellsBelow5) + " of " + std::to_string(c.cells) +
                      " cells have expected counts below 5; minimum expected = " + stats::fmt2(c.minExpected) +
                      "). The Chi-Square approximation may be unreliable. Collect more data or combine neighbouring categories "
                      "(for example Poor + Fair) before relying on this result.";
    }
    causation = "Association is not causation. Even a strong association does not show that exercising changes fitness: fitness may also "
                "influence how much people exercise, and age, health conditions or lifestyle could affect both. Fitness is self-reported, so "
                "ratings are subjective.";
    if (d.synthetic)
        synth = "This dataset is SYNTHETIC (computer-generated for demonstration). The results illustrate the method only and are not findings from a real survey.";
    else if (d.bundled)
        synth = "Data: CDC BRFSS 2015 (real survey responses), unweighted random sample. Survey weights are not applied, so results describe this sample, not the whole US population. 'Fitness' is the respondent's self-rated general health, used as a proxy.";
    w.key("interpretation").beginObject();
    w.field("headline", headline).field("plain", plain).field("caution", caution).field("causation", causation).field("synthetic", synth);
    w.endObject();

    w.endObject();
    return w.str();
}

inline std::string recordsJson(const ds::Dataset& d) {
    JsonWriter w;
    w.beginObject().key("records").beginArray();
    for (const auto& r : d.records)
        w.beginObject().field("id", r.id).field("e", r.ex).field("f", r.fit).field("line", r.line).endObject();
    w.endArray().endObject();
    return w.str();
}

inline std::string errorJson(const std::string& msg, const std::vector<ds::Issue>& issues = {}) {
    JsonWriter w;
    w.beginObject().field("error", msg).key("issues").beginArray();
    for (const auto& i : issues) w.beginObject().field("line", i.line).field("message", i.message).endObject();
    w.endArray().endObject();
    return w.str();
}

}  // namespace api
