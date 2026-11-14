// =====================================================
// v1.36.0 — headless-проверки формата STL и разбора координат.
//
// До v1.36.0 формат определялся ТОЧНЫМ совпадением размера файла с
// 84 + nt·50. Любой хвост (мусор, выравнивание, дописанные CAD-данные) отправлял
// бинарный файл в ASCII-парсер, и пользователь видел «модель не загрузилась».
// Плюс координаты |x| > 1e6 обнулялись, что расплющивало модели в миллиметрах.
//
// Здесь каждый вариант — отдельный синтетический файл с ОЖИДАЕМЫМ результатом:
//   бинарный точного размера, бинарный с 16 лишними байтами, бинарный с nt = 0,
//   ASCII, «solid + бинарь» (бинарный с текстовым заголовком), обрезок,
//   треугольник с NaN, координаты в тысячах единиц.
// =====================================================

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "stl_loader.h"
#include "stl_format.h"

static int g_checks = 0, g_failed = 0;
static void check(bool cond, const std::string& what) {
    ++g_checks;
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failed;
}

// -----------------------------------------------------
// Синтетические файлы
// -----------------------------------------------------
static void putFloat(std::vector<unsigned char>& d, float f) {
    unsigned char b[4];
    std::memcpy(b, &f, 4);
    d.insert(d.end(), b, b + 4);
}
static void putU32(std::vector<unsigned char>& d, std::uint32_t v) {
    d.push_back((unsigned char)(v & 0xff));
    d.push_back((unsigned char)((v >> 8) & 0xff));
    d.push_back((unsigned char)((v >> 16) & 0xff));
    d.push_back((unsigned char)((v >> 24) & 0xff));
}

// Бинарный STL: header (ровно 80 байт), nt, затем 50 байт на треугольник.
static std::vector<unsigned char> makeBinary(std::uint32_t triangles,
                                             const std::string& header = "binary aeros test",
                                             std::size_t trailing = 0,
                                             bool nanFirst = false) {
    std::vector<unsigned char> d;
    d.assign(80, 0);
    for (size_t i = 0; i < header.size() && i < 80; ++i) d[i] = (unsigned char)header[i];
    putU32(d, triangles);
    for (std::uint32_t i = 0; i < triangles; ++i) {
        // нормаль
        putFloat(d, 0.0f); putFloat(d, 0.0f); putFloat(d, 1.0f);
        float base = (float)i * 2.0f;
        const float verts[9] = {
            base, 0.0f, 0.0f,
            base + 1.0f, 0.0f, 0.0f,
            base, 1.0f, 0.0f
        };
        for (int k = 0; k < 9; ++k) putFloat(d, verts[k]);
        d.push_back(0); d.push_back(0);   // attribute byte count
    }
    if (nanFirst && d.size() >= 84 + 12 + 12) {
        // Портим первую вершину первого треугольника: NaN в X.
        const float nan = std::nanf("");
        std::memcpy(d.data() + 84 + 12 + 0, &nan, 4);
    }
    for (std::size_t i = 0; i < trailing; ++i) d.push_back((unsigned char)(0xA5));
    return d;
}

static std::string makeAscii(int triangles, bool solid = true) {
    std::string s;
    if (solid) s += "solid test\n";
    for (int i = 0; i < triangles; ++i) {
        s += "  facet normal 0 0 1\n    outer loop\n";
        s += "      vertex 0 0 0\n      vertex 1 0 0\n      vertex 0 1 0\n";
        s += "    endloop\n  endfacet\n";
    }
    if (solid) s += "endsolid test\n";
    return s;
}

static std::string writeTemp(const std::string& name, const std::vector<unsigned char>& data) {
    static int counter = 0;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "/tmp/aeros-stl-%d-%s", counter++, name.c_str());
    std::ofstream f(buf, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
    return buf;
}

static std::string writeTemp(const std::string& name, const std::string& text) {
    return writeTemp(name, std::vector<unsigned char>(text.begin(), text.end()));
}

int main() {
    std::printf("=== формат STL и разбор координат (v1.36.0) ===\n");

    // ---------- 1. Бинарный, точный размер ----------
    std::printf("\n--- бинарный, точный размер ---\n");
    {
        const auto d = makeBinary(2);
        const auto det = stlformat::detect(d.data(), d.size());
        check(det.format == stlformat::Format::Binary, "определён как бинарный");
        check(det.declaredTriangles == 2, "число треугольников из заголовка = 2");
        check(det.trailingBytes == 0, "хвоста нет");

        std::vector<float> v, n;
        const std::string path = writeTemp("bin_exact.stl", d);
        check(loadSTL(path, v, n), "файл загружен");
        check(v.size() == 2 * 9 && n.size() == 2 * 9, "2 треугольника × 3 вершины × 3 координаты");
    }

    // ---------- 2. Бинарный с хвостом ----------
    std::printf("\n--- бинарный с 16 лишними байтами ---\n");
    {
        const auto d = makeBinary(3, "binary aeros test", 16);
        const auto det = stlformat::detect(d.data(), d.size());
        check(det.format == stlformat::Format::Binary, "хвост не ломает определение");
        check(det.trailingBytes == 16, "хвост распознан (16 байт)");

        std::vector<float> v, n;
        const std::string path = writeTemp("bin_tail.stl", d);
        check(loadSTL(path, v, n), "файл с хвостом загружен (раньше уходил в ASCII)");
        check(v.size() == 3 * 9, "прочитаны все 3 треугольника, хвост проигнорирован");
    }

    // ---------- 3. nt = 0 ----------
    std::printf("\n--- бинарный с nt = 0 ---\n");
    {
        const auto d = makeBinary(0);
        const auto det = stlformat::detect(d.data(), d.size());
        check(det.format != stlformat::Format::Binary, "nt = 0 не считается бинарным");
        std::vector<float> v, n;
        check(!loadSTL(writeTemp("bin_zero.stl", d), v, n), "загрузка отклонена");
        check(v.empty(), "вершины не появились");
    }

    // ---------- 4. ASCII ----------
    std::printf("\n--- ASCII ---\n");
    {
        const std::string s = makeAscii(2);
        const auto det = stlformat::detect(
            reinterpret_cast<const unsigned char*>(s.data()), s.size());
        check(det.format == stlformat::Format::Ascii, "определён как ASCII");
        std::vector<float> v, n;
        check(loadSTL(writeTemp("ascii.stl", s), v, n), "файл загружен");
        check(v.size() == 2 * 9, "2 треугольника");
        check(std::fabs(n[2] - 1.0f) < 1e-5f, "нормаль нормализована (0,0,1)");
    }

    // ---------- 5. Бинарный с текстовым заголовком "solid" ----------
    std::printf("\n--- бинарный, заголовок начинается с 'solid' ---\n");
    {
        const auto d = makeBinary(2, "solid exported by CAD");
        const auto det = stlformat::detect(d.data(), d.size());
        check(det.format == stlformat::Format::Binary,
              "'solid' в заголовке не превращает бинарный файл в ASCII");
        std::vector<float> v, n;
        check(loadSTL(writeTemp("bin_solid.stl", d), v, n), "файл загружен");
        check(v.size() == 2 * 9, "2 треугольника");
    }

    // ---------- 6. Обрезок ----------
    std::printf("\n--- обрезанный бинарный файл ---\n");
    {
        auto d = makeBinary(100);
        d.resize(84 + 10 * 50 + 7);   // обещано 100, есть ~10
        const auto det = stlformat::detect(d.data(), d.size());
        check(det.format != stlformat::Format::Binary, "обрезок не считается бинарным");
        check(std::strstr(det.reason, "обрезан") != nullptr,
              std::string("причина названа: ") + det.reason);
        std::vector<float> v, n;
        check(!loadSTL(writeTemp("bin_trunc.stl", d), v, n), "загрузка отклонена");
    }

    // ---------- 7. NaN в координатах ----------
    std::printf("\n--- треугольник с NaN ---\n");
    {
        const auto d = makeBinary(2, "binary", 0, /*nanFirst=*/true);
        std::vector<float> v, n;
        check(loadSTL(writeTemp("bin_nan.stl", d), v, n), "файл загружен, брак пропущен");
        check(v.size() == 1 * 9, "остался 1 корректный треугольник из 2");
        bool allFinite = true;
        for (float x : v) if (!std::isfinite(x)) allFinite = false;
        check(allFinite, "в результате нет NaN/Inf");
    }

    // ---------- 8. Большие координаты не обнуляются ----------
    std::printf("\n--- координаты в миллиметрах (тысячи единиц) ---\n");
    {
        // Треугольник с координатами ~1250 — типичный STL в миллиметрах.
        std::vector<unsigned char> d;
        d.assign(80, 0);
        const char* hdr = "binary mm model";
        std::memcpy(d.data(), hdr, std::strlen(hdr));
        putU32(d, 1);
        putFloat(d, 0.0f); putFloat(d, 0.0f); putFloat(d, 1.0f);
        const float verts[9] = {1250.0f, 0.0f, 0.0f,
                                1251.0f, 0.0f, 0.0f,
                                1250.0f, 1.0f, 0.0f};
        for (float f : verts) putFloat(d, f);
        d.push_back(0); d.push_back(0);

        std::vector<float> v, n;
        check(loadSTL(writeTemp("bin_mm.stl", d), v, n), "файл загружен");
        check(v.size() == 9, "треугольник не отброшен");
        check(std::fabs(v[0] - 1250.0f) < 1e-3f,
              "координата 1250 сохранена (раньше обнулялась порогом 1e6)");
        check(std::fabs(v[3] - 1251.0f) < 1e-3f, "вторая вершина тоже сохранена");
    }

    // ---------- 9. Мусор ----------
    std::printf("\n--- мусор вместо STL ---\n");
    {
        const std::string junk = "hello, this is not an stl file at all\n";
        const auto det = stlformat::detect(
            reinterpret_cast<const unsigned char*>(junk.data()), junk.size());
        check(det.format == stlformat::Format::Unknown, "мусор не распознан как модель");
        std::vector<float> v, n;
        check(!loadSTL(writeTemp("junk.stl", junk), v, n), "загрузка отклонена");
    }
    {
        const auto det = stlformat::detect(nullptr, 0);
        check(det.format == stlformat::Format::Unknown, "пустой буфер безопасен");
    }

    std::printf("\n=== STL: проверок %d, ошибок %d ===\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
