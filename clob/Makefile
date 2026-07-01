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
	rm -f $(TARGET) $(OBJS) $(TEST_TARGET) $(TEST_OBJS)

# ---------------- Tests (arm64 GoogleTest from native Homebrew at /opt/homebrew) ----------------
# -I (not -isystem) so the arm64 headers win over the Intel gtest in /usr/local/include;
# -Wno-character-conversion silences a warning inside gtest's own headers.
GTEST_CFLAGS := -I/opt/homebrew/include -Wno-character-conversion
GTEST_LIBS   := -L/opt/homebrew/lib -lgtest -lgtest_main

TEST_TARGET := run_tests
TEST_SRCS   := tests/test_orderbook.cpp src/engine/OrderBook.cpp
TEST_OBJS   := $(TEST_SRCS:.cpp=.test.o)

$(TEST_TARGET): $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_OBJS) $(GTEST_LIBS)

%.test.o: %.cpp
	$(CXX) $(CXXFLAGS) $(GTEST_CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

.PHONY: clean test
