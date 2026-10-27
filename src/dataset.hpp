// Modules 1b + 2: dataset validation, missing-value handling, frequency counting.
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "csv_parser.hpp"

namespace ds {

constexpr int K = 5;  // 5 categories for each variable

inline const std::array<std::string, K>& exerciseLabels() {
    static const std::array<std::string, K> v = {
        "Never", "Rarely", "1\xE2\x80\x93" "2 days per week", "3\xE2\x80\x93" "4 days per week",
        "5+ days per week"};
    return v;
}
inline const std::array<std::string, K>& fitnessLabels() {
    static const std::array<std::string, K> v = {"Poor", "Fair", "Good", "Very Good", "Excellent"};
    return v;
}

struct Record {
    std::string id;
    int ex = -1;
    int fit = -1;
    size_t line = 0;
};
struct Issue {
    size_t line;
    std::string message;
};
struct Dataset {
    std::string name;
    std::vector<Record> records;
    size_t totalRows = 0, missingRows = 0, invalidRows = 0;
    std::vector<Issue> issues;       // capped list of rejected rows
    std::vector<std::string> warnings;
    bool synthetic = false;
    bool hasId = false;
    bool bundled = false;      // the sample shipped with the project
    std::string citation;      // source description shown in the UI
};
struct LoadResult {
    bool ok = false;
    std::string error;
    std::vector<Issue> issues;
    Dataset ds;
};

// ---------- normalisation helpers ----------
inline void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), to); p += to.size(); }
}
inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
// lower-case, unify dashes, drop spaces/underscores/NBSP
inline std::string squash(std::string s) {
    replaceAll(s, "\xE2\x80\x93", "-");
    replaceAll(s, "\xE2\x80\x94", "-");
    replaceAll(s, "\xE2\x88\x92", "-");
    replaceAll(s, "\xC2\xA0", "");
    std::string o;
    for (unsigned char c : s) {
        if (std::isspace(c) || c == '_') continue;
        o += static_cast<char>(std::tolower(c));
    }
    return o;
}
inline bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
inline bool isMissing(const std::string& raw) {
    static const std::set<std::string> m = {"", "na", "n/a", "nan", "null", "missing", "?", "-", "--", "."};
    return m.count(squash(raw)) > 0;
}
inline int parseExercise(const std::string& raw) {
    std::string s = squash(raw);
    static const char* suffixes[] = {"daysperweek", "dayperweek", "daysaweek", "dayaweek", "days/week",
                                     "day/week",    "x/week",     "xperweek", "perweek",  "/week",
                                     "days",        "day"};
    for (const char* suf : suffixes)
        if (endsWith(s, suf) && s.size() > std::string(suf).size()) { s.erase(s.size() - std::string(suf).size()); break; }
    if (s == "never" || s == "none" || s == "0" || s == "no") return 0;
    if (s == "rarely" || s == "seldom" || s == "rare" || s == "lessthanonceaweek" || s == "<1") return 1;
    if (s == "1-2" || s == "1to2" || s == "1or2") return 2;
    if (s == "3-4" || s == "3to4" || s == "3or4") return 3;
    if (s == "5+" || s == "5plus" || s == "5ormore" || s == "5-7" || s == "5to7" || s == "5andabove" ||
        s == "5orabove")
        return 4;
    return -1;
}
inline int parseFitness(const std::string& raw) {
    std::string s = squash(raw);
    s.erase(std::remove(s.begin(), s.end(), '-'), s.end());
    if (s == "poor") return 0;
    if (s == "fair") return 1;
    if (s == "good") return 2;
    if (s == "verygood") return 3;
    if (s == "excellent") return 4;
    return -1;
}

inline int findColumn(const csv::Row& header, std::initializer_list<const char*> names) {
    for (size_t i = 0; i < header.size(); ++i) {
        std::string h;
        for (unsigned char c : header[i]) if (std::isalnum(c)) h += static_cast<char>(std::tolower(c));
        for (const char* n : names) if (h == n) return static_cast<int>(i);
    }
    return -1;
}

// ---------- loading / validation ----------
inline LoadResult loadFromText(const std::string& text, const std::string& name) {
    LoadResult res;
    res.ds.name = name;
    csv::Parsed p = csv::parse(text);
    if (!p.error.empty()) { res.error = p.error; return res; }
    if (p.rows.empty()) { res.error = "The file is empty."; return res; }

    const csv::Row& header = p.rows[0];
    int exCol = findColumn(header, {"exercisefrequency", "exercise", "exercisefreq", "frequency"});
    int fitCol = findColumn(header, {"selfreportedfitness", "fitness", "fitnesslevel", "selfreportedfitnesslevel"});
    int idCol = findColumn(header, {"participantid", "id", "participant"});
    int srcCol = findColumn(header, {"source", "datasource"});
    if (exCol < 0 || fitCol < 0) {
        std::string found;
        for (const auto& h : header) found += (found.empty() ? "" : ", ") + std::string("\"") + trim(h) + "\"";
        res.error = std::string("Required column(s) not found: ") + (exCol < 0 ? "ExerciseFrequency " : "") +
                    (fitCol < 0 ? "SelfReportedFitness " : "") +
                    ". Expected a comma-separated file with a header row. Columns found: " + found + ".";
        return res;
    }
    Dataset& d = res.ds;
    d.hasId = idCol >= 0;
    const size_t need = static_cast<size_t>(std::max(exCol, fitCol)) + 1;
    std::set<std::string> seen;
    size_t dupIds = 0, sourceSynthetic = 0, sourceRows = 0;
    const size_t kMaxIssues = 50;

    auto addIssue = [&](size_t line, std::string msg) {
        if (d.issues.size() < kMaxIssues) d.issues.push_back({line, std::move(msg)});
    };

    for (size_t r = 1; r < p.rows.size(); ++r) {
        const csv::Row& row = p.rows[r];
        const size_t line = p.lines[r];
        bool allEmpty = true;
        for (const auto& f : row) if (!trim(f).empty()) { allEmpty = false; break; }
        if (allEmpty) continue;  // stray blank rows (e.g. Excel exports)
        ++d.totalRows;

        if (row.size() < need) {
            ++d.invalidRows;
            addIssue(line, "Row has " + std::to_string(row.size()) + " column(s) but at least " +
                               std::to_string(need) + " are required.");
            continue;
        }
        const std::string& exRaw = row[exCol];
        const std::string& fitRaw = row[fitCol];
        if (srcCol >= 0 && static_cast<size_t>(srcCol) < row.size()) {
            ++sourceRows;
            if (squash(row[srcCol]).find("synthetic") != std::string::npos) ++sourceSynthetic;
        }
        if (isMissing(exRaw) || isMissing(fitRaw)) { ++d.missingRows; continue; }

        int ex = parseExercise(exRaw), fit = parseFitness(fitRaw);
        if (ex < 0) {
            ++d.invalidRows;
            addIssue(line, "Unrecognised Exercise Frequency value \"" + trim(exRaw) +
                               "\". Allowed: Never, Rarely, 1\xE2\x80\x93" "2 days per week, 3\xE2\x80\x93" "4 days per week, 5+ days per week.");
            continue;
        }
        if (fit < 0) {
            ++d.invalidRows;
            addIssue(line, "Unrecognised Self-Reported Fitness value \"" + trim(fitRaw) +
                               "\". Allowed: Poor, Fair, Good, Very Good, Excellent.");
            continue;
        }
        Record rec;
        rec.ex = ex;
        rec.fit = fit;
        rec.line = line;
        rec.id = (idCol >= 0 && static_cast<size_t>(idCol) < row.size() && !trim(row[idCol]).empty())
                     ? trim(row[idCol])
                     : "row-" + std::to_string(line);
        if (idCol >= 0 && !seen.insert(rec.id).second) ++dupIds;
        d.records.push_back(std::move(rec));
    }

    if (d.records.empty()) {
        res.error = "No valid participant records were found (" + std::to_string(d.totalRows) + " data row(s) read, " +
                    std::to_string(d.missingRows) + " with missing values, " + std::to_string(d.invalidRows) +
                    " invalid).";
        res.issues = d.issues;
        return res;
    }
    if (d.missingRows)
        d.warnings.push_back(std::to_string(d.missingRows) + " row(s) with a missing Exercise Frequency or Fitness value were excluded (listwise deletion).");
    if (d.invalidRows)
        d.warnings.push_back(std::to_string(d.invalidRows) + " row(s) with invalid or malformed values were rejected and excluded.");
    if (dupIds)
        d.warnings.push_back(std::to_string(dupIds) + " duplicate participant ID(s) found; duplicates were kept - check your data.");
    if (d.records.size() < 30)
        d.warnings.push_back("Only " + std::to_string(d.records.size()) + " valid records: a sample this small rarely satisfies the Chi-Square assumptions.");
    d.synthetic = sourceRows > 0 && sourceSynthetic * 2 >= sourceRows;
    res.ok = true;
    return res;
}

// ---------- frequency calculations ----------
struct Freq {
    std::array<long, K> counts{};
    std::array<double, K> percents{};
    long total = 0;
    int mode = 0;  // index of the most common category
};
inline Freq frequencies(const std::vector<Record>& recs, bool exercise) {
    Freq f;
    for (const auto& r : recs) { ++f.counts[exercise ? r.ex : r.fit]; ++f.total; }
    for (int i = 0; i < K; ++i) {
        f.percents[i] = f.total ? 100.0 * f.counts[i] / f.total : 0.0;
        if (f.counts[i] > f.counts[f.mode]) f.mode = i;
    }
    return f;
}

}  // namespace ds
