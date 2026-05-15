CXX = g++
CXXFLAGS = -std=c++17 -O2

MAIN = src/main.cpp
PLOT = src/plot.py

IN_DIR = experiment/input
OUT_DIR = experiment/output

CONFIGS = $(IN_DIR)/configs.cfg
TARGET = $(OUT_DIR)/megha.out
RESULTS = $(OUT_DIR)/results.csv
LOGS = $(OUT_DIR)/console.log

.PHONY: all build run plot clean

all: build

build: $(TARGET)

$(TARGET): $(MAIN)
	$(CXX) $(CXXFLAGS) $< -o $@

run: build
	stdbuf -oL ./$(TARGET) $(CONFIGS) | tee $(LOGS)

plot:
	python3 $(PLOT) $(RESULTS)

clean:
	rm -f $(TARGET) $(OUT_DIR)/*