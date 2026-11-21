#version 450
// =====================================================
// Aeros Engine — v1.33.0, фрагментный шейдер сцены для Vulkan.
// Диффузное освещение (ламберт) + заполняющий свет, как в модели OpenGL-ветки:
// ambient 0.35 + 0.75 * max(dot(n, l), 0). Для линий и частиц освещение
// отключено (pc.lightDirUse.w == 0) и цвет берётся как есть.
// =====================================================
layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 lightDirUse;
    vec4 tintAlpha;
    vec4 camPosPoint;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    const bool lit = (pc.lightDirUse.w > 0.5);
    vec3 c = vColor;
    if (lit) {
        vec3 n = normalize(vNormal);
        float diff = max(dot(n, normalize(pc.lightDirUse.xyz)), 0.0);
        c *= (0.35 + 0.75 * diff);
    }
    outColor = vec4(clamp(c, vec3(0.0), vec3(1.0)), clamp(pc.tintAlpha.a, 0.0, 1.0));
}
