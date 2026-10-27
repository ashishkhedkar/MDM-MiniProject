// Generates the clearly-labelled SYNTHETIC dataset (fixed seed => reproducible).
// Usage: ./build/generate_dataset [records=300] [output=data/participants_synthetic.csv]
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

int main(int argc, char** argv) {
    int n = argc > 1 ? std::atoi(argv[1]) : 300;
    std::string out = argc > 2 ? argv[2] : "data/participants_synthetic.csv";
    const char* ex[5] = {"Never", "Rarely", "1\xE2\x80\x93" "2 days per week", "3\xE2\x80\x93" "4 days per week", "5+ days per week"};
    const char* fit[5] = {"Poor", "Fair", "Good", "Very Good", "Excellent"};
    const double exP[5] = {0.10, 0.17, 0.26, 0.29, 0.18};
    // P(fitness | exercise): built-in positive association so the demo shows a pattern
    const double fitP[5][5] = {{0.30, 0.34, 0.24, 0.09, 0.03}, {0.18, 0.34, 0.30, 0.14, 0.04},
                               {0.07, 0.22, 0.38, 0.24, 0.09}, {0.03, 0.10, 0.34, 0.35, 0.18},
                               {0.01, 0.05, 0.20, 0.38, 0.36}};
    std::mt19937 rng(20261025);
    std::discrete_distribution<int> dEx(exP, exP + 5);
    std::discrete_distribution<int> dFit[5] = {{fitP[0], fitP[0] + 5}, {fitP[1], fitP[1] + 5}, {fitP[2], fitP[2] + 5},
                                               {fitP[3], fitP[3] + 5}, {fitP[4], fitP[4] + 5}};
    std::ofstream f(out, std::ios::binary);
    if (!f) { std::cerr << "Cannot write " << out << "\n"; return 1; }
    f << "ParticipantID,ExerciseFrequency,SelfReportedFitness,Source\n";
    for (int i = 1; i <= n; ++i) {
        int e = dEx(rng), s = dFit[e](rng);
        char id[16];
        snprintf(id, sizeof id, "P%04d", i);
        f << id << ',' << ex[e] << ',' << fit[s] << ",Synthetic\n";
    }
    std::cout << "Wrote " << n << " synthetic records to " << out << "\n";
}
