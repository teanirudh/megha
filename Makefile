CXX = g++
CXXFLAGS = -std=c++17 -O2

SOURCE = src/main.cpp
TARGET = megha

.PHONY: all build run plot clean

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE)

build: $(TARGET)

run: $(TARGET)
	cd exp && ../$(TARGET) configs.cfg

plot:
	python3 exp/plot.py --csv exp/results.csv --pdf exp/figures.pdf

clean:
	rm -f $(TARGET) exp/results.csv exp/figures.pdf exp/trace.log