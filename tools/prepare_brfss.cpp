// Builds the project dataset from the CDC BRFSS 2015 raw file (2015.csv).
//   ExerciseFrequency <- EXERANY2 (any exercise, past 30 days) + EXEROFT1 (times per week/month of main activity)
//   SelfReportedFitness <- GENHLTH (self-rated general health: Excellent..Poor)  [proxy, see README]
// Usage: ./build/prepare_brfss <2015.csv> [sample_size=5000] [output=data/brfss2015_sample.csv]
// Rows with refused / don't-know / missing answers are dropped; a fixed-seed random sample is written.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

static std::vector<std::string> splitCsvLine(const std::string& s) {
    std::vector<std::string> f; std::string cur; bool q = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (q) { if (c == '"') { if (i + 1 < s.size() && s[i + 1] == '"') { cur += '"'; ++i; } else q = false; } else cur += c; }
        else if (c == '"') q = true;
        else if (c == ',') { f.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    f.push_back(cur);
    return f;
}
static bool toNum(const std::string& s, double& v) {
    if (s.empty() || s == ".") return false;
    char* e = nullptr; v = std::strtod(s.c_str(), &e);
    return e != s.c_str() && *e == '\0';
}
struct Rec { size_t order; std::string id; int ex, fit; };

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: prepare_brfss <2015.csv> [sample_size] [output]\n"; return 1; }
    size_t N = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 5000;
    std::string out = argc > 3 ? argv[3] : "data/brfss2015_sample.csv";
    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "Cannot open " << argv[1] << "\n"; return 1; }
    std::string line;
    std::getline(in, line);
    auto h = splitCsvLine(line);
    int cG = -1, cA = -1, cO = -1, cS = -1;
    for (size_t i = 0; i < h.size(); ++i) {
        if (h[i] == "GENHLTH") cG = i; else if (h[i] == "EXERANY2") cA = i;
        else if (h[i] == "EXEROFT1") cO = i; else if (h[i] == "SEQNO") cS = i;
    }
    if (cG < 0 || cA < 0 || cO < 0 || cS < 0) { std::cerr << "Needs columns GENHLTH, EXERANY2, EXEROFT1, SEQNO (use BRFSS 2013 or 2015 file).\n"; return 1; }
    std::mt19937 rng(2015);
    std::vector<Rec> res; size_t seen = 0, total = 0, valid = 0;
    while (std::getline(in, line)) {
        ++total;
        auto f = splitCsvLine(line);
        if (f.size() <= static_cast<size_t>(std::max({cG, cA, cO, cS}))) continue;
        double g, a, o = 0;
        if (!toNum(f[cG], g) || g < 1 || g > 5) continue;
        if (!toNum(f[cA], a) || (a != 1 && a != 2)) continue;
        int ex;
        if (a == 2) ex = 0;                       // no exercise in past 30 days -> Never
        else {
            if (!toNum(f[cO], o)) continue;
            double perWeek;
            if (o >= 101 && o <= 199) perWeek = o - 100;             // times per week
            else if (o >= 201 && o <= 299) perWeek = (o - 200) / (30.0 / 7.0);  // times per month -> per week
            else continue;
            ex = perWeek < 1 ? 1 : perWeek < 3 ? 2 : perWeek < 5 ? 3 : 4;
        }
        std::string id = f[cS]; size_t dot = id.find('.'); if (dot != std::string::npos) id.resize(dot);
        Rec r{total, "B15-" + id, ex, static_cast<int>(6 - g) - 1};   // GENHLTH 1=Excellent..5=Poor -> idx 4..0
        ++valid; ++seen;
        if (res.size() < N) res.push_back(r);
        else { size_t j = std::uniform_int_distribution<size_t>(0, seen - 1)(rng); if (j < N) res[j] = r; }
    }
    std::sort(res.begin(), res.end(), [](const Rec& x, const Rec& y) { return x.order < y.order; });
    const char* ex[5] = {"Never", "Rarely", "1\xE2\x80\x93" "2 days per week", "3\xE2\x80\x93" "4 days per week", "5+ days per week"};
    const char* fit[5] = {"Poor", "Fair", "Good", "Very Good", "Excellent"};
    std::ofstream o(out, std::ios::binary);
    if (!o) { std::cerr << "Cannot write " << out << "\n"; return 1; }
    o << "ParticipantID,ExerciseFrequency,SelfReportedFitness,Source\n";
    for (auto& r : res) o << r.id << ',' << ex[r.ex] << ',' << fit[r.fit] << ",CDC BRFSS 2015\n";
    std::cout << "Read " << total << " respondents, " << valid << " with complete answers; wrote random sample of " << res.size() << " to " << out << "\n";
}
