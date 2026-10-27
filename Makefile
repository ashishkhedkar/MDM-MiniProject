CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
SRC      := $(wildcard src/*.cpp) $(wildcard src/*.hpp)

all: build/exercise_fitness

build/exercise_fitness: $(SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/main.cpp -o $@

build/generate_dataset: tools/generate_dataset.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

# Re-create the synthetic dataset (optional - a copy is already in data/)
synthetic: build/generate_dataset
	./build/generate_dataset 300 data/participants_synthetic.csv

run: all
	./build/exercise_fitness 8080

# Optional: download Chart.js for offline use (otherwise the page loads it from a CDN)
vendor:
	mkdir -p web/vendor
	curl -L -o web/vendor/chart.umd.min.js https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js

clean:
	rm -rf build

.PHONY: all dataset run vendor clean

# Rebuild the BRFSS sample from the raw CDC file:  make brfss RAW=/path/to/2015.csv
brfss: tools/prepare_brfss.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o build/prepare_brfss
	./build/prepare_brfss $(RAW) 5000 data/brfss2015_sample.csv
.PHONY: brfss synthetic
