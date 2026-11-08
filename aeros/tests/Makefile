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
#   make startup     только главное меню: стартовый экран, недавние модели, демо-тела (v1.34.0)
#   make glyphs      покрытие глифов: все символы интерфейса есть в шрифте (v1.34.0)
#   make hotkeys     таблица горячих клавиш совпадает с обработчиками (v1.34.0)
#   make validate    валидация по эталонам: Cd сферы/цилиндра, сеточная сходимость (v1.35.0)
#   make openurl     запуск внешних программ без shell и проверка open_url.h (v1.36.0)
#   make voxel       вокселизация: BVH, сходимость объёма, «внутри/снаружи» (v1.36.0)
#   make stl         формат STL: хвост, обрезок, ASCII/бинарный, NaN, координаты (v1.36.0)
#   make physref     эталонные течения: Пуазейль, вязкость, порядок сходимости (v1.36.0)
#
# Тесты намеренно не входят ни в один релизный сборочный скрипт: они линкуют
# заглушки вместо OpenGL/GLFW/CUDA и нужны только для проверки логики.
SRC := ../src
INC := -I$(SRC) -I../../libs/glm -I../../libs/glad/include -I../../libs/glfw/include
# EXTRA_CXXFLAGS — для CI-прогона под ASan/UBSan, не меняя остальные флаги:
#   make EXTRA_CXXFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
EXTRA_CXXFLAGS ?=
CXXFLAGS := -std=c++17 -O1 -fopenmp -Wall -Wextra -Wno-unused-parameter $(INC) $(EXTRA_CXXFLAGS)

COMMON := $(SRC)/globals.cpp $(SRC)/debug_log.cpp

IMGUI := $(SRC)/imgui

.PHONY: all timelapse lbm physclock fem ui settings vulkan particles startup glyphs hotkeys validate openurl voxel stl physref clean
all: timelapse lbm physclock fem ui settings vulkan particles startup glyphs hotkeys validate openurl voxel stl physref

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
	rm -f /tmp/aeros-test-stl /tmp/aeros-test-voxel /tmp/aeros-test-physref /tmp/aeros-test-openurl /tmp/aeros-test-validate /tmp/aeros-test-timelapse /tmp/aeros-test-glyphs /tmp/aeros-test-hotkeys /tmp/aeros-test-lbm /tmp/aeros-test-physclock \
	      /tmp/aeros-test-fem /tmp/aeros-test-particles /tmp/aeros-test-startup

# Физика частиц (v1.34.0). Линкуется с настоящим particles.cpp: проверяется
# именно тот код, который считает частицы в приложении. Заглушки — OpenGL/CUDA.
particles:
	g++ $(CXXFLAGS) particles_headless.cpp $(SRC)/particles.cpp $(SRC)/flow_field.cpp \
	    $(SRC)/voxel_grid.cpp $(SRC)/lbm.cpp $(SRC)/atmosphere.cpp $(COMMON) \
	    -o /tmp/aeros-test-particles -pthread
	/tmp/aeros-test-particles

# Главное меню (v1.34.0): стартовый экран рисуется настоящим ImGui, поэтому
# ядро ImGui подключается к сборке. Движок не линкуется: загрузчики модели и
# диалоги заменены управляемыми заглушками — проверяются действия экрана,
# список недавних моделей и геометрия демо-тел.
# Покрытие глифов: тест читает исходники интерфейса, вытаскивает все строковые
# литералы и проверяет, что каждый не-ASCII символ есть и в таблице диапазонов
# aerosUiGlyphRanges(), и в самом файле шрифта.
glyphs:
	g++ $(CXXFLAGS) -I$(IMGUI) ui_glyphs.cpp -o /tmp/aeros-test-glyphs
	/tmp/aeros-test-glyphs $(SRC)

# Таблица горячих клавиш: каждая строка обязана иметь обработчик в исходниках,
# и каждая функциональная клавиша обязана быть в таблице.
hotkeys:
	g++ $(CXXFLAGS) -I$(IMGUI) hotkeys_headless.cpp -o /tmp/aeros-test-hotkeys
	/tmp/aeros-test-hotkeys $(SRC)

# Валидация по опубликованным эталонам — самый долгий набор (минуты): несколько
# прогонов LBM на сетках до 56^3. В `all` входит, потому что именно он ловит
# деградацию физики, которую остальные проверки не видят.
validate:
	g++ $(CXXFLAGS) validation_headless.cpp $(SRC)/lbm.cpp $(SRC)/flow_field.cpp \
	    $(SRC)/voxel_grid.cpp $(SRC)/atmosphere.cpp $(COMMON) \
	    -o /tmp/aeros-test-validate
	/tmp/aeros-test-validate

# Открытие ссылок и диалоги без shell (v1.36.0): проверяется, что спецсимволы
# в URL не превращаются в команды оболочки и что отсутствующий opener — это
# ошибка, а не «успех» (как было с system() и фоновым `&`).
physref:
	g++ $(CXXFLAGS) physics_ref_headless.cpp $(SRC)/lbm.cpp $(SRC)/flow_field.cpp \
	    $(SRC)/voxel_grid.cpp $(SRC)/atmosphere.cpp $(COMMON) \
	    -o /tmp/aeros-test-physref -pthread
	/tmp/aeros-test-physref

openurl:
	g++ $(CXXFLAGS) open_url_headless.cpp -o /tmp/aeros-test-openurl
	/tmp/aeros-test-openurl

# Вокселизация (v1.36.0): BVH против прямого перебора, сходимость объёма на
# аналитических мешах, отсутствие пустых/лишних вокселей, тонкая пластина,
# детерминизм и большая модель. Линкуется настоящий voxel_grid.cpp.
voxel:
	g++ $(CXXFLAGS) voxel_headless.cpp $(SRC)/voxel_grid.cpp $(COMMON) \
	    -o /tmp/aeros-test-voxel -pthread
	/tmp/aeros-test-voxel

# Формат STL (v1.36.0): определение бинарный/ASCII по содержимому, хвост файла,
# обрезок, NaN-треугольники, координаты в миллиметрах.
stl:
	g++ $(CXXFLAGS) stl_headless.cpp $(SRC)/stl_loader.cpp $(COMMON) \
	    -o /tmp/aeros-test-stl -pthread
	/tmp/aeros-test-stl

startup:
	g++ $(CXXFLAGS) -I$(IMGUI) start_screen_headless.cpp \
	    $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_widgets.cpp \
	    $(IMGUI)/imgui_tables.cpp $(COMMON) $(SRC)/timelapse.cpp $(SRC)/lbm.cpp \
	    $(SRC)/flow_field.cpp $(SRC)/voxel_grid.cpp $(SRC)/atmosphere.cpp \
	    -o /tmp/aeros-test-startup -pthread
	/tmp/aeros-test-startup

# Vulkan-бэкенд (v1.33.0) — чистые модули: выбор swapchain/формата/режима показа,
# проверка SPIR-V, раскладка вершины и геометрия сцены. GPU и Vulkan SDK не нужны:
# подключаются только заголовки из репозитория и сгенерированный SPIR-V.
vulkan:
	g++ $(CXXFLAGS) vulkan_headless.cpp -o /tmp/aeros-test-vulkan
	/tmp/aeros-test-vulkan
