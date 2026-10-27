# VitalStat — Exercise & Self-Reported Fitness (Chi-Square mini-project)

Healthcare-domain statistical dashboard. **All statistics are computed in C++17**; the browser (HTML/CSS/JS + Chart.js) only renders results and talks to the C++ server over a local HTTP API. No Python anywhere.

## Dataset
**Real survey data: CDC Behavioral Risk Factor Surveillance System (BRFSS) 2015** (https://www.cdc.gov/brfss/), from the BRFSS 2011–2015 archive you supplied. The project bundles `data/brfss2015_sample.csv`, a fixed-seed random sample of **5,000** of the 397,610 respondents with complete answers (of 441,456 interviewed). It is built by `tools/prepare_brfss.cpp` (C++), reproducible with `make brfss RAW=/path/to/2015.csv`.

| Project variable | BRFSS field | How it is derived |
|---|---|---|
| ExerciseFrequency | EXERANY2 + EXEROFT1 | "No exercise in past 30 days" -> Never. Otherwise sessions of the main activity: per-week counts used directly, per-month counts divided by 30/7. Under 1/week -> Rarely; 1-<3 -> 1–2; 3-<5 -> 3–4; 5+ -> 5+ |
| SelfReportedFitness | GENHLTH | Self-rated *general health* (Poor, Fair, Good, Very Good, Excellent) — **a proxy** for self-reported fitness; BRFSS has no dedicated fitness question |

Limitations: sessions of the main activity are not the same as distinct exercise days; refused / don't-know / missing answers are dropped; BRFSS survey weights are **not** applied, so results describe the sample, not the US population.
A computer-generated **synthetic** file (`data/participants_synthetic.csv`, `make synthetic`) is also included for testing uploads; it is labelled as synthetic in the UI whenever loaded.

## Run (macOS / Linux, needs a C++17 compiler)

```bash
cd exercise-fitness
make            # compiles build/exercise_fitness  (g++ -std=c++17 -O2 -pthread)
make run        # starts the server  ->  open http://localhost:8080
```
Other port: `./build/exercise_fitness 9000`. Always start it from the project root.
Chart.js loads from a CDN; for fully offline use run `make vendor` once (downloads it to `web/vendor/`).
Windows: use WSL.

## Project structure
```
src/csv_parser.hpp    CSV parsing (quotes, BOM, CRLF, # comments)
src/dataset.hpp       validation, category normalisation, missing values, frequencies
src/contingency.hpp   observed/expected tables, row/col/cell percentages
src/chi_square.hpp    chi-square statistic, df, p-value (incomplete gamma), assumption checks
src/cramers_v.hpp     Cramér's V + Cohen strength thresholds
src/json_builder.hpp  JSON writer, API payloads, plain-language interpretation
src/http_server.hpp   dependency-free HTTP server (POSIX sockets, 127.0.0.1 only)
src/main.cpp          routes, static files, results CSV export
web/                  index.html, css/style.css, js/app.js
data/                 brfss2015_sample.csv, participants_synthetic.csv, test_dirty_sample.csv, results_*.csv (generated)
tools/prepare_brfss.cpp     builds the BRFSS sample from the raw CDC file
tools/generate_dataset.cpp  optional synthetic test data
```

## API
| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/analysis` | distributions, contingency table, test, Cramér's V, interpretation |
| GET | `/api/records` | participant records for the explorer |
| POST | `/api/upload` | body = CSV text (`X-Filename` header); validates, recomputes, returns analysis or 400 + error list |
| POST | `/api/reset` | restore the synthetic sample |
| GET | `/api/export/{summary,cells,dataset}` | results / cell table / current dataset as CSV |

## CSV format
Header row with `ExerciseFrequency` and `SelfReportedFitness` (optional `ParticipantID`, `Source`). Exercise: Never, Rarely, 1–2 days per week, 3–4 days per week, 5+ days per week (hyphens, case and "days/week" variants accepted). Fitness: Poor, Fair, Good, Very Good, Excellent. Blank / NA / N/A / null → missing, excluded. Invalid values are rejected with line numbers. Try `data/test_dirty_sample.csv`.

## Statistical notes
H0: independent; H1: associated; α = 0.05. Expected counts are checked (≤20% of cells below 5, minimum ≥ 1); violations show a warning in the UI. Empty categories are dropped before computing df. Association is not causation, and fitness is self-reported.
