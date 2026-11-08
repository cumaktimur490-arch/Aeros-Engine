// Отрисовка главного меню БЕЗ окна и видеокарты: настоящий код стартового
// экрана (start_screen.h) выполняется на контексте ImGui, а его списки
// отрисовки растеризуются программно в PPM.
//
// Зачем это нужно: интерфейс нельзя «проверить глазами» в окружении без
// OpenGL/GLFW, а раскладку (не наезжают ли кнопки, читается ли текст, не
// уезжает ли правая колонка при узком окне) хочется увидеть. Здесь
// используются те же данные, что уходят в GPU: ImDrawData, вершинные буферы,
// атлас шрифта. Ничего не «дорисовывается» вручную.
//
// Сборка: make screenshot
#include "imgui.h"
#include "cae_ui.h"
#include "start_screen.h"
#include "ui_font_ranges.h"
#include "recent_files.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- заглушки движка ----
Language currentLanguage = Language::Russian;
bool caeColorOverride = false;
void updateVertexColors() {}
void uploadVertexColorsToGPU() {}
extern "C" double glfwGetTime(void) { return 0.0; }
extern "C" void setVoxelData(const int*, const float*, int, int, int, float, float, float,
                             float, float, float) {}
extern "C" int cudaConsumeError(char*, int) { return 0; }
bool loadModel(const std::string&) { return false; }
bool loadDemoBody(int) { return false; }
std::string openFileDialog(void*) { return std::string(); }
std::string openFileDialogNative() { return std::string(); }
std::string saveFileDialogNative(const std::string&) { return std::string(); }

struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> rgb;   // RGB, сверху вниз
};

// Программная растеризация ImDrawData: треугольники, альфа-смешивание,
// выборка цвета из атласа шрифта с ближайшим соседом (масштаб 1:1).
static Image rasterize(ImDrawData* dd, const unsigned char* tex, int tw, int th, int W, int H) {
    Image img;
    img.w = W; img.h = H;
    img.rgb.assign((size_t)W * H * 3, 0);

    for (int li = 0; li < dd->CmdListsCount; ++li) {
        const ImDrawList* list = dd->CmdLists[li];
        for (int ci = 0; ci < list->CmdBuffer.Size; ++ci) {
            const ImDrawCmd& cmd = list->CmdBuffer[ci];
            if (cmd.UserCallback) continue;
            const int cx0 = (int)std::floor(cmd.ClipRect.x), cy0 = (int)std::floor(cmd.ClipRect.y);
            const int cx1 = (int)std::ceil(cmd.ClipRect.z),  cy1 = (int)std::ceil(cmd.ClipRect.w);
            for (unsigned int e = 0; e + 2 < cmd.ElemCount; e += 3) {
                const unsigned int i0 = list->IdxBuffer[cmd.IdxOffset + e + 0];
                const unsigned int i1 = list->IdxBuffer[cmd.IdxOffset + e + 1];
                const unsigned int i2 = list->IdxBuffer[cmd.IdxOffset + e + 2];
                const ImDrawVert& v0 = list->VtxBuffer[cmd.VtxOffset + i0];
                const ImDrawVert& v1 = list->VtxBuffer[cmd.VtxOffset + i1];
                const ImDrawVert& v2 = list->VtxBuffer[cmd.VtxOffset + i2];

                const float minx = std::min({v0.pos.x, v1.pos.x, v2.pos.x});
                const float maxx = std::max({v0.pos.x, v1.pos.x, v2.pos.x});
                const float miny = std::min({v0.pos.y, v1.pos.y, v2.pos.y});
                const float maxy = std::max({v0.pos.y, v1.pos.y, v2.pos.y});
                int x0 = (int)std::floor(minx), x1 = (int)std::ceil(maxx);
                int y0 = (int)std::floor(miny), y1 = (int)std::ceil(maxy);
                if (x0 < cx0) x0 = cx0;
                if (y0 < cy0) y0 = cy0;
                if (x1 > cx1) x1 = cx1;
                if (y1 > cy1) y1 = cy1;
                if (x0 < 0) x0 = 0;
                if (y0 < 0) y0 = 0;
                if (x1 > W) x1 = W;
                if (y1 > H) y1 = H;
                if (x0 >= x1 || y0 >= y1) continue;

                const float area = (v1.pos.x - v0.pos.x) * (v2.pos.y - v0.pos.y) -
                                   (v2.pos.x - v0.pos.x) * (v1.pos.y - v0.pos.y);
                if (std::fabs(area) < 1e-9f) continue;

                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        const float px = (float)x + 0.5f, py = (float)y + 0.5f;
                        float w0 = ((v1.pos.x - px) * (v2.pos.y - py) - (v2.pos.x - px) * (v1.pos.y - py)) / area;
                        float w1 = ((v2.pos.x - px) * (v0.pos.y - py) - (v0.pos.x - px) * (v2.pos.y - py)) / area;
                        float w2 = 1.0f - w0 - w1;
                        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;

                        const float u = w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x;
                        const float v = w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y;
                        int tx = (int)(u * (float)tw), ty = (int)(v * (float)th);
                        if (tx < 0) tx = 0;
                        if (ty < 0) ty = 0;
                        if (tx >= tw) tx = tw - 1;
                        if (ty >= th) ty = th - 1;
                        const float ta = tex[((size_t)ty * tw + tx) * 4 + 3] / 255.0f;

                        auto chan = [&](int c) {
                            return (w0 * ((v0.col >> (c * 8)) & 0xFF) +
                                    w1 * ((v1.col >> (c * 8)) & 0xFF) +
                                    w2 * ((v2.col >> (c * 8)) & 0xFF));
                        };
                        const float sr = chan(0), sg = chan(1), sb = chan(2), sa = chan(3) / 255.0f;
                        const float a = sa * ta;
                        if (a <= 0.0f) continue;
                        unsigned char* dst = &img.rgb[((size_t)y * W + x) * 3];
                        dst[0] = (unsigned char)std::min(255.0f, dst[0] * (1.0f - a) + sr * a);
                        dst[1] = (unsigned char)std::min(255.0f, dst[1] * (1.0f - a) + sg * a);
                        dst[2] = (unsigned char)std::min(255.0f, dst[2] * (1.0f - a) + sb * a);
                    }
                }
            }
        }
    }
    return img;
}

static bool writePPM(const char* path, const Image& img) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
    std::fwrite(img.rgb.data(), 1, img.rgb.size(), f);
    std::fclose(f);
    return true;
}

int main(int argc, char** argv) {
    const int W = (argc > 1) ? std::atoi(argv[1]) : 1600;
    const int H = (argc > 2) ? std::atoi(argv[2]) : 900;
    const char* out = (argc > 3) ? argv[3] : "/tmp/start_screen.ppm";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DeltaTime = 1.0f / 60.0f;

    // Тот же системный шрифт, что загружает initLocalization() в lang.cpp:
    // дефолтный шрифт ImGui без кириллицы, и русские подписи выглядели бы
    // квадратами. В приложении шрифт ставится до первого кадра.
    const char* fonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        nullptr};
    bool fontLoaded = false;
    for (int i = 0; fonts[i]; ++i) {
        // Диапазон глифов передаётся явно — ровно как в lang.cpp: без него
        // старый (не-«RendererHasTextures») бэкенд не знает, какие символы
        // растеризовать заранее, и кириллица выпадает.
        if (io.Fonts->AddFontFromFileTTF(fonts[i], 18.0f, nullptr, aerosUiGlyphRanges())) {
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) io.Fonts->AddFontDefault();

    // ImGui 1.92 собирает атлас шрифта лениво: глифы добавляются при первом
    // кадре, где они реально нужны. Поэтому сначала «прогревочные» кадры (они
    // же создают атлас), и только потом берём пиксели шрифта — иначе часть
    // кириллицы отсутствует и вместо букв рисуются квадраты.
    // Рендерер здесь «старый» (без RendererHasTextures): атлас обязан быть
    // построен ДО первого кадра, иначе ImGui не знает, откуда брать глифы.
    // Build() + GetTexDataAsRGBA32() в этом режиме заранее растеризуют все
    // символы, включая кириллицу.
    io.Fonts->Build();
    unsigned char* warm_px = nullptr;
    int warm_tw = 0, warm_th = 0;
    io.Fonts->GetTexDataAsRGBA32(&warm_px, &warm_tw, &warm_th);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    // Пара недавних моделей, чтобы список не был пустым.
    recentfiles::clear(recentfiles::defaultPath());
    recentfiles::add("C:\\Models\\wing_naca2412.stl");
    recentfiles::add("D:\\Aeros Engine\\samples\\sphere.stl");

    aeroStartScreen = true;
    UiLayoutState& st = uiLayoutState();
    st.fontScale = 1.0f;

    unsigned char* pixels = nullptr;
    int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    ImGui::NewFrame();
    startscreen::draw(st);
    ImGui::Render();

    const Image img = rasterize(ImGui::GetDrawData(), pixels, tw, th, W, H);
    const bool ok = writePPM(out, img);
    ImGui::DestroyContext();
    std::printf("%s: %s (%dx%d)\n", ok ? "written" : "failed", out, W, H);
    return ok ? 0 : 1;
}
