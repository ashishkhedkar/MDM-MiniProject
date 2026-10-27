// Exercise & Self-Reported Fitness - Chi-Square analysis server.
// All statistics are computed in C++; the browser only renders results.
#include <cctype>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

#include "http_server.hpp"
#include "json_builder.hpp"

namespace {

const std::string kWebDir = "web";
const std::string kDataDir = "data";
const std::string kSamplePath = kDataDir + "/brfss2015_sample.csv";

struct State {
    std::mutex m;
    std::string analysisJson, recordsJson, summaryCsv, cellsCsv, datasetCsv;
} state;

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
bool writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << data;
    return static_cast<bool>(f);
}
std::string csvEscape(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}
std::string num(double v, int prec = 6) {
    if (!std::isfinite(v)) return "";
    std::ostringstream ss;
    ss.precision(prec);
    ss << v;
    return ss.str();
}

// ---- CSV outputs (analysis results are persisted as CSV) ----
std::string buildSummaryCsv(const ds::Dataset& d, const api::Analysis& a) {
    std::ostringstream o;
    o << "metric,value\n";
    o << "dataset," << csvEscape(d.name) << "\n";
    o << "synthetic," << (d.synthetic ? "yes" : "no") << "\n";
    o << "valid_records," << d.records.size() << "\n";
    o << "missing_rows_excluded," << d.missingRows << "\n";
    o << "invalid_rows_rejected," << d.invalidRows << "\n";
    o << "chi_square_statistic," << num(a.chi.statistic) << "\n";
    o << "degrees_of_freedom," << a.chi.df << "\n";
    o << "p_value," << num(a.chi.pValue, 10) << "\n";
    o << "alpha," << a.chi.alpha << "\n";
    o << "decision," << (!a.chi.computable ? "not applicable" : a.chi.significant ? "reject H0" : "fail to reject H0") << "\n";
    o << "cramers_v," << num(a.cv.value) << "\n";
    o << "association_strength," << a.cv.strength << "\n";
    o << "cells_expected_below_5," << a.chi.cellsBelow5 << "\n";
    o << "min_expected_frequency," << num(a.chi.minExpected) << "\n";
    o << "assumptions_met," << (a.chi.assumptionsMet ? "yes" : "no") << "\n";
    return o.str();
}
std::string buildCellsCsv(const api::Analysis& a) {
    std::ostringstream o;
    o << "exercise_frequency,self_reported_fitness,observed,expected,row_percent,column_percent,total_percent,std_residual\n";
    for (int i = 0; i < ds::K; ++i)
        for (int j = 0; j < ds::K; ++j)
            o << csvEscape(ds::exerciseLabels()[i]) << ',' << ds::fitnessLabels()[j] << ',' << a.tab.obs[i][j] << ','
              << num(a.tab.exp[i][j]) << ',' << num(a.tab.rowPct[i][j]) << ',' << num(a.tab.colPct[i][j]) << ','
              << num(a.tab.cellPct[i][j]) << ',' << num(a.chi.resid[i][j]) << '\n';
    return o.str();
}
std::string buildDatasetCsv(const ds::Dataset& d) {
    std::ostringstream o;
    o << "ParticipantID,ExerciseFrequency,SelfReportedFitness,Source\n";
    for (const auto& r : d.records)
        o << csvEscape(r.id) << ',' << csvEscape(ds::exerciseLabels()[r.ex]) << ',' << ds::fitnessLabels()[r.fit] << ','
          << (d.synthetic ? "Synthetic" : "User-supplied") << '\n';
    return o.str();
}

void publish(const ds::Dataset& d) {
    api::Analysis a = api::analyze(d);
    std::string json = api::toJson(d, a), recs = api::recordsJson(d);
    std::string summary = buildSummaryCsv(d, a), cells = buildCellsCsv(a), dataset = buildDatasetCsv(d);
    writeFile(kDataDir + "/results_summary.csv", summary);
    writeFile(kDataDir + "/results_cells.csv", cells);
    std::lock_guard<std::mutex> lock(state.m);
    state.analysisJson = std::move(json);
    state.recordsJson = std::move(recs);
    state.summaryCsv = std::move(summary);
    state.cellsCsv = std::move(cells);
    state.datasetCsv = std::move(dataset);
}

bool loadSample(std::string& err) {
    std::string text;
    if (!readFile(kSamplePath, text)) { err = "Sample dataset not found at " + kSamplePath; return false; }
    ds::LoadResult r = ds::loadFromText(text, "brfss2015_sample.csv");
    if (!r.ok) { err = "Sample dataset is invalid: " + r.error; return false; }
    r.ds.bundled = true;
    r.ds.citation = "CDC Behavioral Risk Factor Surveillance System (BRFSS) 2015 - random sample of 5,000 respondents with complete answers (https://www.cdc.gov/brfss/)";
    publish(r.ds);
    return true;
}

std::string sanitizeName(std::string n) {
    size_t s = n.find_last_of("/\\");
    if (s != std::string::npos) n = n.substr(s + 1);
    std::string o;
    for (unsigned char c : n) if (c >= 0x20 && c != 0x7F) o += static_cast<char>(c);
    if (o.size() > 80) o.resize(80);
    return o.empty() ? "uploaded.csv" : o;
}

std::string mimeFor(const std::string& p) {
    auto ends = [&](const char* e) { return ds::endsWith(p, e); };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".js")) return "application/javascript; charset=utf-8";
    if (ends(".json")) return "application/json; charset=utf-8";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".csv")) return "text/csv; charset=utf-8";
    if (ends(".png")) return "image/png";
    if (ends(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

http::Response json(int status, std::string body) {
    http::Response r;
    r.status = status;
    r.contentType = "application/json; charset=utf-8";
    r.body = std::move(body);
    r.headers["Cache-Control"] = "no-store";
    return r;
}
http::Response csvDownload(const std::string& body, const std::string& filename) {
    http::Response r;
    r.contentType = "text/csv; charset=utf-8";
    r.body = body;
    r.headers["Content-Disposition"] = "attachment; filename=\"" + filename + "\"";
    r.headers["Cache-Control"] = "no-store";
    return r;
}

http::Response serveStatic(const std::string& reqPath) {
    http::Response r;
    std::string p = reqPath == "/" ? "/index.html" : reqPath;
    if (p.find("..") != std::string::npos || p.find('\\') != std::string::npos || p.find('\0') != std::string::npos) {
        r.status = 403; r.body = "Forbidden\n"; return r;
    }
    std::string body;
    if (!readFile(kWebDir + p, body)) { r.status = 404; r.body = "Not found\n"; return r; }
    r.contentType = mimeFor(p);
    r.body = std::move(body);
    r.headers["Cache-Control"] = "no-cache";
    return r;
}

http::Response route(const http::Request& req) {
    const std::string& p = req.path;
    if (p.rfind("/api/", 0) == 0) {
        if (req.method == "GET") {
            std::lock_guard<std::mutex> lock(state.m);
            if (p == "/api/analysis") return json(200, state.analysisJson);
            if (p == "/api/records") return json(200, state.recordsJson);
            if (p == "/api/export/summary") return csvDownload(state.summaryCsv, "results_summary.csv");
            if (p == "/api/export/cells") return csvDownload(state.cellsCsv, "results_cells.csv");
            if (p == "/api/export/dataset") return csvDownload(state.datasetCsv, "current_dataset.csv");
            if (p == "/api/health") return json(200, "{\"status\":\"ok\"}");
            return json(404, api::errorJson("Unknown API endpoint: " + p));
        }
        if (req.method == "POST") {
            if (p == "/api/upload") {
                if (req.body.empty()) return json(400, api::errorJson("The uploaded file is empty."));
                std::string name = sanitizeName(http::urlDecode(req.header("x-filename")));
                ds::LoadResult r = ds::loadFromText(req.body, name);
                if (!r.ok) return json(400, api::errorJson(r.error, r.issues));
                writeFile(kDataDir + "/uploaded.csv", req.body);
                publish(r.ds);
                std::lock_guard<std::mutex> lock(state.m);
                return json(200, state.analysisJson);
            }
            if (p == "/api/reset") {
                std::string err;
                if (!loadSample(err)) return json(500, api::errorJson(err));
                std::lock_guard<std::mutex> lock(state.m);
                return json(200, state.analysisJson);
            }
            return json(404, api::errorJson("Unknown API endpoint: " + p));
        }
        return json(405, api::errorJson("Method not allowed."));
    }
    if (req.method != "GET") { http::Response r; r.status = 405; r.body = "Method not allowed\n"; return r; }
    return serveStatic(p);
}

}  // namespace

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    if (port <= 0 || port > 65535) { std::cerr << "Usage: " << argv[0] << " [port]\n"; return 1; }
    std::string probe;
    if (!readFile(kWebDir + "/index.html", probe)) {
        std::cerr << "Cannot find web/index.html - run this program from the project root folder.\n";
        return 1;
    }
    std::string err;
    if (!loadSample(err)) { std::cerr << err << "\n"; return 1; }
    std::cout << "Exercise & Fitness analysis server ready.\n"
              << "Open  http://localhost:" << port << "  in your browser (Ctrl+C to stop).\n";
    return http::serve(port, route) ? 0 : 1;
}
