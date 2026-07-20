CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra

all: sender receiver

sender: src/sender.cpp
	$(CXX) $(CXXFLAGS) -o sender src/sender.cpp

receiver: src/receiver.cpp
	$(CXX) $(CXXFLAGS) -o receiver src/receiver.cpp

clean:
	rm -f sender receiver
