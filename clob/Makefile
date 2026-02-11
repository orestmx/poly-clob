CXX = clang++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra
TARGET = clob_sim

$(TARGET): main.cpp order_book.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp order_book.cpp

clean:
	rm -f $(TARGET)