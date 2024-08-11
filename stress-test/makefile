# 指定编译器
CXX = g++

# 编译选项
CXXFLAGS = -std=c++11 -pthread

# 目标文件
TARGET = stress

# 源文件
SRCS = stress_test.cpp

# 规则定义
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

# 清理编译生成的文件
clean:
	rm -f $(TARGET)

