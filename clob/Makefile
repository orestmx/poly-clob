CXX      := clang++
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra -I src

TARGET := simulator
SRCS   := src/main.cpp src/engine/OrderBook.cpp
OBJS   := $(SRCS:.cpp=.o)

# First rule = default. Link object files into the executable.
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Pattern rule: build any .o from its matching .cpp
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean
