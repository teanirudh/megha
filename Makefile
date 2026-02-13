CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -I./src

TARGET = kumo_experiment
SRC = src/main_experiment.cpp

.PHONY: all clean reproduce

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

reproduce: $(TARGET)
	bash artifacts_reproduce.sh
