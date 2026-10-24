# Headless-тесты Aeros Engine — собираются и выполняются на Linux без OpenGL/GLFW.
#
#   make            собрать и запустить оба теста
#   make timelapse  только таймлапс физики (v1.26.0)
#   make lbm        только прогон решателя LBM (v1.27.0)
#
# Тесты намеренно не входят ни в один релизный сборочный скрипт: они линкуют
# заглушки вместо OpenGL/GLFW/CUDA и нужны только для проверки логики.
SRC := ../src
INC := -I$(SRC) -I../../libs/glm -I../../libs/glad/include -I../../libs/glfw/include
CXXFLAGS := -std=c++17 -O1 -fopenmp -Wall -Wextra -Wno-unused-parameter $(INC)

COMMON := $(SRC)/globals.cpp $(SRC)/debug_log.cpp

.PHONY: all timelapse lbm clean
all: timelapse lbm

timelapse:
	g++ $(CXXFLAGS) timelapse_headless.cpp $(SRC)/timelapse.cpp $(SRC)/debug_log.cpp \
	    -o /tmp/aeros-test-timelapse -pthread
	/tmp/aeros-test-timelapse

lbm:
	g++ $(CXXFLAGS) lbm_physics_headless.cpp $(SRC)/lbm.cpp $(SRC)/flow_field.cpp \
	    $(SRC)/voxel_grid.cpp $(SRC)/atmosphere.cpp $(COMMON) \
	    -o /tmp/aeros-test-lbm
	/tmp/aeros-test-lbm

clean:
	rm -f /tmp/aeros-test-timelapse /tmp/aeros-test-lbm
