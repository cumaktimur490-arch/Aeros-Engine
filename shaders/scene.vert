#version 450
// =====================================================
// Aeros Engine — v1.33.0, шейдер сцены для Vulkan.
//
// Один вершинный шейдер на все три вида геометрии (модель, частицы, линии):
// отличается только топология конвейера и режим освещения, который приходит
// в pc.lightDirUse.w. Нормали заданы в пространстве модели, поэтому направление
// на свет тоже пересчитывается в пространство модели на стороне процессора
// (reinterpretas: для жёстких и равномерно-масштабированных матриц этого достаточно).
// =====================================================
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(push_constant) uniform Push {
    mat4 mvp;           // модель * вид * проекция
    vec4 lightDirUse;   // xyz — направление на свет (в пространстве модели), w — применять освещение
    vec4 tintAlpha;     // rgb — цвет-множитель, a — прозрачность
    vec4 camPosPoint;   // xyz — камера (не используется), w — размер точки
} pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;

void main() {
    vColor = inColor * pc.tintAlpha.rgb;
    vNormal = inNormal;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    // Размер точки для конвейера частиц; для треугольников и линий не влияет.
    gl_PointSize = max(pc.camPosPoint.w, 1.0);
}
