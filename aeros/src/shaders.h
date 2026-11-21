#ifndef SHADERS_H
#define SHADERS_H

// =====================================================
// GLSL-шейдеры v1.8.0 — улучшенное освещение
// =====================================================

inline const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 FragPos;
out vec3 Normal;
out vec3 VColor;
void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    VColor = aColor;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

inline const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec3 FragPos;
in vec3 Normal;
in vec3 VColor;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 objectColor;
uniform bool useLighting;
uniform bool useVertexColor;
uniform float alpha;
uniform bool useRealisticLighting;
void main() {
    vec3 base = useVertexColor ? VColor : objectColor;
    if (!useLighting) {
        FragColor = vec4(base, alpha);
        return;
    }
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    vec3 viewDir = normalize(viewPos - FragPos);

    if (useRealisticLighting) {
        // PBR-like улучшенное освещение
        float ambientStrength = 0.25;
        vec3 ambient = ambientStrength * lightColor * base;

        float diff = max(dot(norm, lightDir), 0.0);
        // Fresnel-like
        float fresnel = pow(1.0 - max(dot(norm, viewDir), 0.0), 2.0);
        vec3 diffuse = diff * lightColor * base * (0.8 + 0.2*fresnel);

        // Blinn-Phong specular
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), 64.0);
        vec3 specular = spec * lightColor * 0.4;

        // Rim lighting для силуэта как на фото
        float rim = pow(1.0 - max(dot(norm, viewDir), 0.0), 3.0) * 0.3;
        vec3 rimColor = rim * lightColor;

        vec3 result;
        if (useVertexColor) {
            // Для pressure coloring — сохраняем цвет но добавляем освещение
            result = base * (0.6 + 0.4*diff) + specular + rimColor*0.2;
        } else {
            result = ambient + diffuse + specular + rimColor;
        }
        FragColor = vec4(result, alpha);
    } else {
        float ambientStrength = 0.35;
        vec3 ambient = ambientStrength * lightColor;
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * lightColor;
        float specularStrength = 0.3;
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
        vec3 specular = specularStrength * spec * lightColor;
        vec3 lit = ambient + diffuse + specular;
        vec3 result;
        if (useVertexColor) {
            result = base * (0.7 + 0.3 * diff);
        } else {
            result = lit * base;
        }
        FragColor = vec4(result, alpha);
    }
}
)";

inline const char* particleVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float pointSize;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    gl_PointSize = pointSize;
}
)";

inline const char* particleFragmentShaderSource = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    // Круглая частица с мягкими краями
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);
    if (dist > 0.5) discard;
    float alpha = 1.0 - smoothstep(0.3, 0.5, dist);
    FragColor = vec4(vColor, alpha);
}
)";

inline const char* lineVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform bool useVertexColor;
out vec3 vColor;
void main() {
    vColor = useVertexColor ? aColor : vec3(1.0);
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

inline const char* lineFragmentShaderSource = R"(
#version 330 core
in vec3 vColor;
uniform vec3 lineColor;
uniform bool useVertexColor;
uniform float alpha;
out vec4 FragColor;
void main() {
    vec3 c = useVertexColor ? vColor : lineColor;
    FragColor = vec4(c, alpha);
}
)";

inline const char* groundVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 FragPos;
out vec3 Normal;
void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

inline const char* groundFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec3 FragPos;
in vec3 Normal;
uniform vec3 groundColor;
uniform float alpha;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
void main() {
    // Шахматная земля + туман
    float checker = mod(floor(FragPos.x*0.5) + floor(FragPos.z*0.5), 2.0);
    vec3 base = groundColor + checker*0.05;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    float dist = length(viewPos - FragPos);
    float fog = clamp((dist - 20.0)/30.0, 0.0, 0.6);
    vec3 result = base * (0.4 + 0.6*diff);
    result = mix(result, vec3(0.05,0.06,0.09), fog);
    FragColor = vec4(result, alpha*(1.0-fog*0.5));
}
)";

#endif // SHADERS_H
