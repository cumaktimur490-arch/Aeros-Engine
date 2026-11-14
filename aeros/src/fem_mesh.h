#ifndef AEROS_FEM_MESH_H
#define AEROS_FEM_MESH_H
// =====================================================
// Aeros CAE — объёмная гексаэдральная сетка (Q1, 8 узлов)
//
// ЧЕСТНО О СЕТКЕ. STL-поверхность объёмной FEM-сеткой не является: это набор
// треугольников без топологии тела. Поэтому поверхность сначала вокселизируется
// (движок уже делает это для решателя LBM — g_voxelData/g_distanceField), а
// затем по заполненным ячейкам строятся гексаэдральные элементы Q1. Это тот же
// подход, который используют воксельные мезшеры инженерных пакетов (ScanIP,
// воксельный mesher SimScale, сетка BeamNG): геометрия приближается ступенькой
// размером с ячейку, зато сетка всегда совместна (conforming), без висячих
// узлов, и её можно измельчать для оценки сходимости.
//
// Следствия, о которых программа сообщает явно (см. warnings):
//   - поверхность тела ступенчатая с шагом ячейки;
//   - объём и площади граней считаются по ячейкам, а не по треугольникам;
//   - элементы Q1 при изгибе дают завышенную жёсткость (shear locking),
//     поэтому перемещения при изгибе занижены — лечится измельчением сетки
//     (сравнение на нескольких разрешениях есть в headless-тесте);
//   - тело может развалиться на несвязные части: это ловится проверкой
//     связности, иначе матрица жёсткости сингулярна.
//
// Все размеры здесь в СИ (метры), чтобы ядро не зависело от единиц интерфейса.
// =====================================================

#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <queue>
#include <algorithm>

struct FemMesh {
    // домен и шаг
    int    nx = 0, ny = 0, nz = 0;      // числа УЗЛОВ по осям (= элементы + 1)
    int    ex = 0, ey = 0, ez = 0;      // числа ЭЛЕМЕНТОВ по осям
    double ox = 0, oy = 0, oz = 0;      // начало домена, м
    double hx = 1, hy = 1, hz = 1;      // размер ячейки, м

    // узлы
    std::vector<char> nodeSolid;        // nx*ny*nz: узел принадлежит телу
    std::vector<char> nodeBoundary;     // узел на внешней границе тела
    std::vector<int>  nodeIndex;        // плотный индекс узла тела (-1 для прочих)
    std::vector<char> nodeIsBoundary;   // плотный индекс -> узел на внешней границе
    std::vector<char> nodeFixedX, nodeFixedY, nodeFixedZ; // защемление по компонентам
    std::vector<char> nodeFixedT;       // заданная температура (тепловая задача)

    // элементы
    std::vector<char> elemSolid;        // ex*ey*ez
    std::vector<int>  elemIndex;        // плотный индекс твёрдого элемента (-1)
    std::vector<int>  elemMaterial;     // индекс материала на элемент
    std::vector<int>  elemComponent;    // номер несвязной части тела

    // внешние грани (для давления, теплового потока, конвекции, площади)
    struct Face {
        int   elem;        // плотный индекс элемента
        int   nodes[4];    // плотные индексы узлов грани
        float nx, ny, nz;  // внешняя нормаль
        double area;       // м2
        double cx, cy, cz; // центр грани, м
    };
    std::vector<Face> faces;

    int    nodeCount = 0, elemCount = 0;
    double volume = 0, surfaceArea = 0, massKg = 0;
    int    components = 0;
    double aspectWorst = 1.0;
    bool   built = false;
    std::vector<std::string> warnings;

    // индексация
    inline int nodeId(int i, int j, int k) const { return (k * ny + j) * nx + i; }
    inline int elemId(int i, int j, int k) const { return (k * ey + j) * ex + i; }
    inline bool nodeValid(int i, int j, int k) const {
        return i >= 0 && j >= 0 && k >= 0 && i < nx && j < ny && k < nz;
    }
    // координаты узла по плотному индексу не хранятся: они восстанавливаются
    inline void nodeCoords(int idx, int& i, int& j, int& k) const {
        // idx — плотный; обратное отображение хранится в nodePos
        if (idx >= 0 && idx < (int)nodeI.size()) { i = nodeI[idx]; j = nodeJ[idx]; k = nodeK[idx]; }
        else { i = j = k = -1; }
    }
    std::vector<int> nodeI, nodeJ, nodeK;   // обратная индексация плотных узлов

    inline double nodeX(int idx) const { return ox + (idx >= 0 && idx < (int)nodeI.size() ? nodeI[idx] : 0) * hx; }
    inline double nodeY(int idx) const { return oy + (idx >= 0 && idx < (int)nodeJ.size() ? nodeJ[idx] : 0) * hy; }
    inline double nodeZ(int idx) const { return oz + (idx >= 0 && idx < (int)nodeK.size() ? nodeK[idx] : 0) * hz; }

    // 8 узлов элемента (плотные индексы). Порядок согласован с функциями форм:
    //   0=(0,0,0) 1=(1,0,0) 2=(1,1,0) 3=(0,1,0)
    //   4=(0,0,1) 5=(1,0,1) 6=(1,1,1) 7=(0,1,1)
    inline void elemNodes(int ei, int ej, int ek, int out[8]) const {
        out[0] = nodeIndex[nodeId(ei,   ej,   ek  )];
        out[1] = nodeIndex[nodeId(ei+1, ej,   ek  )];
        out[2] = nodeIndex[nodeId(ei+1, ej+1, ek  )];
        out[3] = nodeIndex[nodeId(ei,   ej+1, ek  )];
        out[4] = nodeIndex[nodeId(ei,   ej,   ek+1)];
        out[5] = nodeIndex[nodeId(ei+1, ej,   ek+1)];
        out[6] = nodeIndex[nodeId(ei+1, ej+1, ek+1)];
        out[7] = nodeIndex[nodeId(ei,   ej+1, ek+1)];
    }
    inline void elemNodesByIdx(int denseElem, int out[8]) const {
        if (denseElem < 0 || denseElem >= (int)elemI.size()) { for (int q=0;q<8;++q) out[q] = -1; return; }
        elemNodes(elemI[denseElem], elemJ[denseElem], elemK[denseElem], out);
    }
    std::vector<int> elemI, elemJ, elemK;   // обратная индексация плотных элементов
};

// -----------------------------------------------------
// Построение сетки по воксельной занятости.
//
// occ           — occupancy исходной воксельной сетки (1 = внутри тела)
// vnx,vny,vnz   — её размеры
// vmin*, vmax*  — её габарит в метрах
// targetRes     — сколько элементов вдоль наибольшей стороны
// materialIndex — материал для всех элементов (однородное тело)
//
// Возвращает true, если сетка построена и содержит хотя бы один элемент.
// Предупреждения складываются в mesh.warnings — они же показываются в дереве
// проекта и попадают в отчёт.
// -----------------------------------------------------
inline bool femMeshBuildFromVoxels(FemMesh& mesh,
                                   const std::vector<int>& occ,
                                   int vnx, int vny, int vnz,
                                   double vminx, double vminy, double vminz,
                                   double vmaxx, double vmaxy, double vmaxz,
                                   int targetRes, int materialIndex,
                                   double densityKgM3)
{
    mesh = FemMesh();
    if (occ.empty() || vnx < 1 || vny < 1 || vnz < 1) {
        mesh.warnings.push_back("нет воксельного представления геометрии — постройте сетку модели");
        return false;
    }
    if ((long long)vnx * vny * vnz != (long long)occ.size()) {
        mesh.warnings.push_back("размер воксельного массива не совпадает с его размерами");
        return false;
    }
    const double spanX = vmaxx - vminx, spanY = vmaxy - vminy, spanZ = vmaxz - vminz;
    const double maxSpan = std::max(spanX, std::max(spanY, spanZ));
    if (!(maxSpan > 0)) {
        mesh.warnings.push_back("габарит модели нулевой — сетку построить нельзя");
        return false;
    }
    if (targetRes < 4) targetRes = 4;
    if (targetRes > 256) { targetRes = 256; mesh.warnings.push_back("разрешение ограничено 256 элементами"); }

    // Число элементов по осям пропорционально физическим размерам, чтобы ячейки
    // были близки к кубу (иначе матрица жёсткости плохо обусловлена).
    mesh.ex = std::max(2, (int)std::lround(targetRes * spanX / maxSpan));
    mesh.ey = std::max(2, (int)std::lround(targetRes * spanY / maxSpan));
    mesh.ez = std::max(2, (int)std::lround(targetRes * spanZ / maxSpan));
    mesh.nx = mesh.ex + 1; mesh.ny = mesh.ey + 1; mesh.nz = mesh.ez + 1;
    mesh.ox = vminx; mesh.oy = vminy; mesh.oz = vminz;
    mesh.hx = spanX / mesh.ex; mesh.hy = spanY / mesh.ey; mesh.hz = spanZ / mesh.ez;

    const long long totalNodes = (long long)mesh.nx * mesh.ny * mesh.nz;
    const long long totalElems = (long long)mesh.ex * mesh.ey * mesh.ez;
    // Предел выбран по памяти, а не «чтобы просто был»: расчёт держит около
    // 250 байт на элемент (векторы метода сопряжённых градиентов, системные
    // массивы, узловые поля результатов), то есть 12 млн элементов — это уже
    // порядка 3 ГБ. На 32-битной сборке адресное пространство вдвое меньше,
    // поэтому лучше честный отказ здесь, чем std::bad_alloc в середине решения.
    const long long kMaxCells = 12000000LL;
    if (totalNodes > kMaxCells || totalElems > kMaxCells) {
        // Буфер с запасом: сообщение по-русски, а в UTF-8 кириллица занимает два
        // байта на символ, поэтому 224 байта не хватало и текст обрывался на
        // «рас…». Три размера сетки — до 11 знаков каждый.
        char b[384];
        std::snprintf(b, sizeof(b),
                      "сетка %dx%dx%d слишком велика: нужно не более %lld элементов "
                      "(расчёт требует около 250 байт памяти на элемент) — уменьшите разрешение",
                      mesh.ex, mesh.ey, mesh.ez, kMaxCells);
        mesh = FemMesh();
        mesh.warnings.push_back(b);
        return false;
    }

    mesh.nodeSolid.assign((size_t)totalNodes, 0);
    mesh.nodeBoundary.assign((size_t)totalNodes, 0);
    mesh.nodeIndex.assign((size_t)totalNodes, -1);
    mesh.elemSolid.assign((size_t)totalElems, 0);
    mesh.elemIndex.assign((size_t)totalElems, -1);
    mesh.elemMaterial.assign((size_t)totalElems, materialIndex);
    mesh.elemComponent.assign((size_t)totalElems, -1);

    const double vcx = (vnx > 0) ? spanX / vnx : 0.0;   // размер исходного вокселя
    const double vcy = (vny > 0) ? spanY / vny : 0.0;
    const double vcz = (vnz > 0) ? spanZ / vnz : 0.0;

    // 1) элементы: центр элемента попадает в тело, если большинство исходных
    //    вокселей, накрытых элементом, заполнено. Для грубой исходной сетки
    //    (типичный случай) это фактически выборка ближайшего вокселя.
    for (int k = 0; k < mesh.ez; ++k)
    for (int j = 0; j < mesh.ey; ++j)
    for (int i = 0; i < mesh.ex; ++i) {
        const double cx = mesh.ox + (i + 0.5) * mesh.hx;
        const double cy = mesh.oy + (j + 0.5) * mesh.hy;
        const double cz = mesh.oz + (k + 0.5) * mesh.hz;
        int vi = (int)std::floor((cx - vminx) / (vcx > 0 ? vcx : 1.0));
        int vj = (int)std::floor((cy - vminy) / (vcy > 0 ? vcy : 1.0));
        int vk = (int)std::floor((cz - vminz) / (vcz > 0 ? vcz : 1.0));
        vi = std::min(std::max(vi, 0), vnx - 1);
        vj = std::min(std::max(vj, 0), vny - 1);
        vk = std::min(std::max(vk, 0), vnz - 1);
        const int solid = occ[((size_t)vk * vny + vj) * vnx + vi] ? 1 : 0;
        if (solid) mesh.elemSolid[(size_t)mesh.elemId(i, j, k)] = 1;
    }

    // 2) плотная нумерация элементов и объём
    for (int k = 0; k < mesh.ez; ++k)
    for (int j = 0; j < mesh.ey; ++j)
    for (int i = 0; i < mesh.ex; ++i) {
        const size_t id = (size_t)mesh.elemId(i, j, k);
        if (!mesh.elemSolid[id]) continue;
        mesh.elemIndex[id] = mesh.elemCount;
        mesh.elemI.push_back(i); mesh.elemJ.push_back(j); mesh.elemK.push_back(k);
        mesh.elemCount++;
        mesh.volume += mesh.hx * mesh.hy * mesh.hz;
    }
    if (mesh.elemCount == 0) {
        mesh.warnings.push_back("в теле не оказалось ни одной заполненной ячейки: увеличьте разрешение сетки или проверьте вокселизацию модели");
        return false;
    }
    mesh.massKg = mesh.volume * densityKgM3;

    // 3) узлы: узел принадлежит телу, если он является углом хотя бы одного
    //    твёрдого элемента. Висячих узлов не бывает — сетка совместная.
    for (int k = 0; k < mesh.ez; ++k)
    for (int j = 0; j < mesh.ey; ++j)
    for (int i = 0; i < mesh.ex; ++i) {
        if (!mesh.elemSolid[(size_t)mesh.elemId(i, j, k)]) continue;
        for (int dk = 0; dk <= 1; ++dk)
        for (int dj = 0; dj <= 1; ++dj)
        for (int di = 0; di <= 1; ++di)
            mesh.nodeSolid[(size_t)mesh.nodeId(i + di, j + dj, k + dk)] = 1;
    }
    for (int k = 0; k < mesh.nz; ++k)
    for (int j = 0; j < mesh.ny; ++j)
    for (int i = 0; i < mesh.nx; ++i) {
        const size_t id = (size_t)mesh.nodeId(i, j, k);
        if (!mesh.nodeSolid[id]) continue;
        mesh.nodeIndex[id] = mesh.nodeCount;
        mesh.nodeI.push_back(i); mesh.nodeJ.push_back(j); mesh.nodeK.push_back(k);
        mesh.nodeCount++;
    }
    mesh.nodeIsBoundary.assign((size_t)mesh.nodeCount, 0);
    mesh.nodeFixedX.assign((size_t)mesh.nodeCount, 0);
    mesh.nodeFixedY.assign((size_t)mesh.nodeCount, 0);
    mesh.nodeFixedZ.assign((size_t)mesh.nodeCount, 0);
    mesh.nodeFixedT.assign((size_t)mesh.nodeCount, 0);

    // 4) внешние грани и узлы на границе. Грань внешняя, если соседний элемент
    //    не твёрдый (или его нет). Нормаль смотрит наружу тела.
    //    Порядок локальных граней: 0=-x 1=+x 2=-y 3=+y 4=-z 5=+z
    static const int FD[6][3] = { {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1} };
    // Узлы каждой грани: обход против часовой стрелки, если смотреть снаружи тела.
    // Для распределения нагрузки порядок не важен (сила = p*n*A/4 на узел), но он
    // нужен, чтобы нормали граней и обходы не разъезжались при доработках.
    static const int FN[6][4][3] = {
        { {0,0,0},{0,0,1},{0,1,1},{0,1,0} },   // -x
        { {1,0,0},{1,1,0},{1,1,1},{1,0,1} },   // +x
        { {0,0,0},{1,0,0},{1,0,1},{0,0,1} },   // -y
        { {0,1,0},{0,1,1},{1,1,1},{1,1,0} },   // +y
        { {0,0,0},{0,1,0},{1,1,0},{1,0,0} },   // -z  (обход 0-3-2-1: нормаль (p1-p0)x(p2-p1) = -z)
        { {0,0,1},{1,0,1},{1,1,1},{0,1,1} }    // +z
    };

    mesh.faces.reserve((size_t)mesh.elemCount * 2);
    for (int k = 0; k < mesh.ez; ++k)
    for (int j = 0; j < mesh.ey; ++j)
    for (int i = 0; i < mesh.ex; ++i) {
        const size_t id = (size_t)mesh.elemId(i, j, k);
        if (!mesh.elemSolid[id]) continue;
        const int denseE = mesh.elemIndex[id];
        for (int f = 0; f < 6; ++f) {
            const int ni = i + FD[f][0], nj = j + FD[f][1], nk = k + FD[f][2];
            bool neighSolid = false;
            if (ni >= 0 && nj >= 0 && nk >= 0 && ni < mesh.ex && nj < mesh.ey && nk < mesh.ez)
                neighSolid = mesh.elemSolid[(size_t)mesh.elemId(ni, nj, nk)] != 0;
            if (neighSolid) continue;

            FemMesh::Face fc;
            fc.elem = denseE;
            for (int q = 0; q < 4; ++q) {
                const int gi = i + FN[f][q][0], gj = j + FN[f][q][1], gk = k + FN[f][q][2];
                const size_t nid = (size_t)mesh.nodeId(gi, gj, gk);
                fc.nodes[q] = mesh.nodeIndex[nid];
                mesh.nodeBoundary[nid] = 1;
            }
            fc.nx = (float)FD[f][0]; fc.ny = (float)FD[f][1]; fc.nz = (float)FD[f][2];
            if (f == 0 || f == 1) fc.area = mesh.hy * mesh.hz;
            else if (f == 2 || f == 3) fc.area = mesh.hx * mesh.hz;
            else fc.area = mesh.hx * mesh.hy;
            double ccx = mesh.ox + (i + 0.5) * mesh.hx + 0.5 * FD[f][0] * mesh.hx;
            double ccy = mesh.oy + (j + 0.5) * mesh.hy + 0.5 * FD[f][1] * mesh.hy;
            double ccz = mesh.oz + (k + 0.5) * mesh.hz + 0.5 * FD[f][2] * mesh.hz;
            fc.cx = ccx; fc.cy = ccy; fc.cz = ccz;
            mesh.faces.push_back(fc);
            mesh.surfaceArea += fc.area;
        }
    }
    // узлы на границе домена, у которых все элементы твёрдые, тоже граничные,
    // если они лежат на внешней поверхности тела: помечаем по отсутствию соседа
    for (int k = 0; k < mesh.nz; ++k)
    for (int j = 0; j < mesh.ny; ++j)
    for (int i = 0; i < mesh.nx; ++i) {
        const size_t id = (size_t)mesh.nodeId(i, j, k);
        if (!mesh.nodeSolid[id]) continue;
        if (mesh.nodeBoundary[id]) continue;
        // если ни один соседний элемент не пуст, узел внутренний
        bool anyEmpty = false;
        for (int dk = -1; dk <= 0 && !anyEmpty; ++dk)
        for (int dj = -1; dj <= 0 && !anyEmpty; ++dj)
        for (int di = -1; di <= 0 && !anyEmpty; ++di) {
            const int ei = i + di, ej = j + dj, ek = k + dk;
            if (ei < 0 || ej < 0 || ek < 0 || ei >= mesh.ex || ej >= mesh.ey || ek >= mesh.ez) continue;
            if (!mesh.elemSolid[(size_t)mesh.elemId(ei, ej, ek)]) anyEmpty = true;
        }
        if (anyEmpty) mesh.nodeBoundary[id] = 1;
    }

    // плотный массив граничных узлов: условия «только на поверхности» обращаются
    // к нему по плотному индексу, а не по (i,j,k)
    for (int k = 0; k < mesh.nz; ++k)
    for (int j = 0; j < mesh.ny; ++j)
    for (int i = 0; i < mesh.nx; ++i) {
        const size_t id = (size_t)mesh.nodeId(i, j, k);
        const int dense = mesh.nodeIndex[id];
        if (dense >= 0 && mesh.nodeBoundary[id]) mesh.nodeIsBoundary[(size_t)dense] = 1;
    }

    // 5) несвязные части тела: BFS по элементам. Плавающая часть без закреплений
    //    делает систему сингулярной — об этом нужно сказать до решения.
    std::vector<int> stack;
    stack.reserve((size_t)mesh.elemCount);
    std::vector<long long> compSizes;
    for (int e = 0; e < mesh.elemCount; ++e) {
        if (mesh.elemComponent[e] >= 0) continue;
        const int comp = mesh.components++;
        stack.clear();
        stack.push_back(e);
        mesh.elemComponent[e] = comp;
        long long size = 0;
        while (!stack.empty()) {
            const int cur = stack.back(); stack.pop_back();
            size++;
            const int ci = mesh.elemI[cur], cj = mesh.elemJ[cur], ck = mesh.elemK[cur];
            for (int d = 0; d < 6; ++d) {
                const int ni = ci + FD[d][0], nj = cj + FD[d][1], nk = ck + FD[d][2];
                if (ni < 0 || nj < 0 || nk < 0 || ni >= mesh.ex || nj >= mesh.ey || nk >= mesh.ez) continue;
                const size_t nid = (size_t)mesh.elemId(ni, nj, nk);
                if (!mesh.elemSolid[nid]) continue;
                const int nd = mesh.elemIndex[nid];
                if (nd < 0 || mesh.elemComponent[nd] >= 0) continue;
                mesh.elemComponent[nd] = comp;
                stack.push_back(nd);
            }
        }
        compSizes.push_back(size);
    }
    // Очень мелкая часть — обычно шум вокселизации: на ней нельзя задать
    // условия, и она даёт сингулярность. Сообщаем явно.
    for (size_t c = 0; c < compSizes.size(); ++c) {
        if (compSizes[c] < 4 && mesh.components > 1) {
            char b[288];
            std::snprintf(b, sizeof(b),
                          "часть тела №%zu состоит из %lld элемент(ов) — вероятно, шум вокселизации; "
                          "увеличьте разрешение или уберите её из геометрии", c + 1, compSizes[c]);
            mesh.warnings.push_back(b);
        }
    }

    // 6) предупреждения о качестве
    {   // соотношение сторон ячейки: максимум/минимум по трём рёбрам
        const double hmin = std::min(mesh.hx, std::min(mesh.hy, mesh.hz));
        const double hmax = std::max(mesh.hx, std::max(mesh.hy, mesh.hz));
        mesh.aspectWorst = (hmin > 0.0) ? hmax / hmin : 1.0;
    }
    {
        char b[220];
        std::snprintf(b, sizeof(b),
                      "сетка %d x %d x %d = %d элементов, %d узлов, объём %.6g м3, площадь поверхности %.6g м2",
                      mesh.ex, mesh.ey, mesh.ez, mesh.elemCount, mesh.nodeCount, mesh.volume, mesh.surfaceArea);
        mesh.warnings.push_back(b);   // первая строка — сводка, не проблема
        if (mesh.components > 1) {
            std::snprintf(b, sizeof(b),
                          "тело состоит из %d несвязных частей: каждая должна быть закреплена отдельно, "
                          "иначе система сингулярна", mesh.components);
            mesh.warnings.push_back(b);
        }
        if (mesh.aspectWorst > 3.0) {
            std::snprintf(b, sizeof(b),
                          "ячейки вытянуты (соотношение сторон %.2f): точность по короткой оси ниже",
                          mesh.aspectWorst);
            mesh.warnings.push_back(b);
        }
        std::snprintf(b, sizeof(b),
                      "геометрия приближена ступенькой %.4g м (размер ячейки) — поверхность тела не гладкая",
                      std::max(mesh.hx, std::max(mesh.hy, mesh.hz)));
        mesh.warnings.push_back(b);
    }

    mesh.built = true;
    return true;
}

// -----------------------------------------------------
// Классификация узлов и граней по областям условий (StudyRegion).
// tol — допуск в метрах (половина ячейки), чтобы область, заданная точно по
// габариту модели, захватила крайние узлы.
// -----------------------------------------------------
inline double femMeshTolerance(const FemMesh& m) {
    return 0.5 * std::max(m.hx, std::max(m.hy, m.hz)) + 1e-9;
}

inline bool femNodeInBox(const FemMesh& m, int nodeIdx, double mnx, double mny, double mnz,
                         double mxx, double mxy, double mxz, double tol) {
    const double x = m.nodeX(nodeIdx), y = m.nodeY(nodeIdx), z = m.nodeZ(nodeIdx);
    return x >= mnx - tol && x <= mxx + tol &&
           y >= mny - tol && y <= mxy + tol &&
           z >= mnz - tol && z <= mxz + tol;
}

#endif // AEROS_FEM_MESH_H
