#ifndef SHADERS_H
#define SHADERS_H

// =====================================================
// GLSL-шейдеры (общие для всех модулей)
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
void main() {
    vec3 base = useVertexColor ? VColor : objectColor;
    if (!useLighting) {
        FragColor = vec4(base, alpha);
        return;
    }
    float ambientStrength = 0.35;
    vec3 ambient = ambientStrength * lightColor;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    float specularStrength = 0.3;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * lightColor;
    vec3 lit = ambient + diffuse + specular;
    vec3 result;
    if (useVertexColor) {
        result = base * (0.7f + 0.3f * diff);
    } else {
        result = lit * base;
    }
    FragColor = vec4(result, alpha);
}
)";

inline const char* particleVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

inline const char* particleFragmentShaderSource = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
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

#endif // SHADERS_H
