# Headless-тесты Aeros Engine — собираются и выполняются на Linux без OpenGL/GLFW.
#
#   make             собрать и запустить все тесты
#   make timelapse   только таймлапс физики (v1.26.0)
#   make lbm         только прогон решателя LBM (v1.27.0)
#   make physclock   только фиксированный такт физики (v1.29.0)
#   make fem         только CAD/CAE: исследование, сетка МКЭ, решатели, компоновка (v1.31.0)
#   make ui          только оболочка CAD/CAE: панели, меню, обработчики кнопок 
#   make settings    только реестр настроек: сохранение, загрузка, сброс, профили (v1.32.0)
#   make vulkan      только Vulkan-бэкенд: выбор swapchain/формата/режима, SPIR-V, геометрия сцены (v1.33.0)
#   make particles   только физика частиц: устойчивость шага, терминальная скорость, единицы SDF (v1.34.0)
#
# Тесты намеренно не входят ни в один релизный сборочный скрипт: они линкуют
# заглушки вместо OpenGL/GLFW/CUDA и нужны только для проверки логики.
SRC := ../src
INC := -I$(SRC) -I../../libs/glm -I../../libs/glad/include -I../../libs/glfw/include
CXXFLAGS := -std=c++17 -O1 -fopenmp -Wall -Wextra -Wno-unused-parameter $(INC)

COMMON := $(SRC)/globals.cpp $(SRC)/debug_log.cpp

IMGUI := $(SRC)/imgui

.PHONY: all timelapse lbm physclock fem ui settings vulkan particles clean
all: timelapse lbm physclock fem ui settings vulkan particles

timelapse:
	g++ $(CXXFLAGS) timelapse_headless.cpp $(SRC)/timelapse.cpp $(SRC)/debug_log.cpp \
	    -o /tmp/aeros-test-timelapse -pthread
	/tmp/aeros-test-timelapse

lbm:
	g++ $(CXXFLAGS) lbm_physics_headless.cpp $(SRC)/lbm.cpp $(SRC)/flow_field.cpp \
	    $(SRC)/voxel_grid.cpp $(SRC)/atmosphere.cpp $(COMMON) \
	    -o /tmp/aeros-test-lbm
	/tmp/aeros-test-lbm

physclock:
	g++ $(CXXFLAGS) phys_clock_headless.cpp -o /tmp/aeros-test-physclock
	/tmp/aeros-test-physclock

# Реестр настроек (settings_store.h) — чистый модуль: тянет только переменные из
# globals.cpp, atmosphere.cpp (высота, единицы скорости) и lang.cpp (язык).
# lang.cpp обращается к ImGui::GetIO() при смене языка, поэтому ядро ImGui
# подключается к сборке (без GLFW/OpenGL-бэкендов), как в цели ui.
settings:
	g++ $(CXXFLAGS) -I$(IMGUI) settings_headless.cpp $(SRC)/globals.cpp \
	    $(SRC)/atmosphere.cpp $(SRC)/lang.cpp $(SRC)/debug_log.cpp \
	    $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_widgets.cpp \
	    $(IMGUI)/imgui_tables.cpp -o /tmp/aeros-test-settings -pthread
	/tmp/aeros-test-settings

# CAD/CAE-модули (study.h, fem_mesh.h, fem_solver.h, results_view.h, ui_layout.h)
# намеренно header-only и не тянут OpenGL/GLFW/ImGui, поэтому тест линкуется без
# единого .cpp движка: проверяются сами расчёты, а не обвязка.
fem:
	g++ $(CXXFLAGS) fem_headless.cpp -o /tmp/aeros-test-fem
	/tmp/aeros-test-fem

# Оболочка CAD/CAE (cae_ui.h, ui_dock.h, ui_layout.h) собирается с НАСТОЯЩИМ ядром
# ImGui в headless-режиме: без GLFW/OpenGL-бэкендов, с заглушками для диалогов выбора
# файлов, загрузки модели и выгрузки цветов вершин в GPU. Так проверяются вещи, которые
# не видны в расчётных тестах: сбалансирован ли стек окон ImGui при любом размере окна
# и при Begin()==false, выполняются ли обработчики кнопок, честно ли оболочка отказывает
# на пустом проекте и не падает ли на мусоре в настройках компоновки.
ui:
	g++ $(CXXFLAGS) -I$(IMGUI) cae_ui_headless.cpp \
	    $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_widgets.cpp \
	    $(IMGUI)/imgui_tables.cpp $(COMMON) $(SRC)/timelapse.cpp $(SRC)/lbm.cpp \
	    $(SRC)/flow_field.cpp $(SRC)/voxel_grid.cpp $(SRC)/atmosphere.cpp \
	    -o /tmp/aeros-test-ui -pthread
	/tmp/aeros-test-ui

clean:
	rm -f /tmp/aeros-test-timelapse /tmp/aeros-test-lbm /tmp/aeros-test-physclock \
	      /tmp/aeros-test-fem /tmp/aeros-test-particles

# Физика частиц (v1.34.0). Линкуется с настоящим particles.cpp: проверяется
# именно тот код, который считает частицы в приложении. Заглушки — OpenGL/CUDA.
particles:
	g++ $(CXXFLAGS) particles_headless.cpp $(SRC)/particles.cpp $(SRC)/flow_field.cpp \
	    $(SRC)/voxel_grid.cpp $(SRC)/lbm.cpp $(SRC)/atmosphere.cpp $(COMMON) \
	    -o /tmp/aeros-test-particles -pthread
	/tmp/aeros-test-particles

# Vulkan-бэкенд (v1.33.0) — чистые модули: выбор swapchain/формата/режима показа,
# проверка SPIR-V, раскладка вершины и геометрия сцены. GPU и Vulkan SDK не нужны:
# подключаются только заголовки из репозитория и сгенерированный SPIR-V.
vulkan:
	g++ $(CXXFLAGS) vulkan_headless.cpp -o /tmp/aeros-test-vulkan
	/tmp/aeros-test-vulkan
