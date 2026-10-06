#include "fsr.h"
#include "globals.h"
#include "gl_utils.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>

// =====================================================
// FSR Scale — как в AMD FSR 1.0
// =====================================================
float getFSRScale(int mode) {
    switch ((FSRMode)mode) {
        case FSRMode::UltraQuality: return 0.77f;
        case FSRMode::Quality: return 0.67f;
        case FSRMode::Balanced: return 0.59f;
        case FSRMode::Performance: return 0.50f;
        case FSRMode::UltraPerformance: return 0.33f;
        default: return 1.0f;
    }
}

const char* getFSRModeName(int mode) {
    switch ((FSRMode)mode) {
        case FSRMode::Off: return "Off (Native)";
        case FSRMode::UltraQuality: return "Ultra Quality (77%)";
        case FSRMode::Quality: return "Quality (67%)";
        case FSRMode::Balanced: return "Balanced (59%)";
        case FSRMode::Performance: return "Performance (50%)";
        case FSRMode::UltraPerformance: return "Ultra Perf (33%)";
        default: return "Unknown";
    }
}

// =====================================================
// Shaders — FSR 1.0 inspired EASU + RCAS
// Упрощённая но эффективная реализация
// =====================================================

static const char* quadVertSrc = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// EASU — Edge Adaptive Spatial Upsampling
// Аппроксимация AMD FSR 1.0 EASU: 12-tap с детекцией краёв
static const char* easuFragSrc = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uLowRes;
uniform vec2 uLowResSize;
uniform vec2 uDisplaySize;
uniform vec2 uInputViewport; // lowRes size
uniform vec2 uOutputViewport; // display size

// Luma
float luma(vec3 c){ return dot(c, vec3(0.2126,0.7152,0.0722)); }

void main() {
    vec2 uv = vUV;
    vec2 texelSize = 1.0 / uLowResSize;

    // Центральный пиксель и соседи — 12 taps как в FSR
    vec3 c  = texture(uLowRes, uv).rgb;

    // Соседи для градиента
    vec3 n  = texture(uLowRes, uv + vec2(0, texelSize.y)).rgb;
    vec3 s  = texture(uLowRes, uv - vec2(0, texelSize.y)).rgb;
    vec3 e  = texture(uLowRes, uv + vec2(texelSize.x, 0)).rgb;
    vec3 w  = texture(uLowRes, uv - vec2(texelSize.x, 0)).rgb;

    vec3 nw = texture(uLowRes, uv + vec2(-texelSize.x, texelSize.y)).rgb;
    vec3 ne = texture(uLowRes, uv + vec2(texelSize.x, texelSize.y)).rgb;
    vec3 sw = texture(uLowRes, uv + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 se = texture(uLowRes, uv + vec2(texelSize.x, -texelSize.y)).rgb;

    // Дальние для анизотропии
    vec3 n2 = texture(uLowRes, uv + vec2(0, texelSize.y*2.0)).rgb;
    vec3 s2 = texture(uLowRes, uv - vec2(0, texelSize.y*2.0)).rgb;
    vec3 e2 = texture(uLowRes, uv + vec2(texelSize.x*2.0, 0)).rgb;
    vec3 w2 = texture(uLowRes, uv - vec2(texelSize.x*2.0, 0)).rgb;

    // Детекция края через luma градиент
    float lC = luma(c);
    float lN = luma(n), lS = luma(s), lE = luma(e), lW = luma(w);
    float lNW = luma(nw), lNE = luma(ne), lSW = luma(sw), lSE = luma(se);

    // Горизонтальный и вертикальный градиент (Sobel)
    float gx = (lNE + 2.0*lE + lSE) - (lNW + 2.0*lW + lSW);
    float gy = (lNW + 2.0*lN + lNE) - (lSW + 2.0*lS + lSE);
    float edge = sqrt(gx*gx + gy*gy);

    // Анизотропия — вдоль края сглаживаем меньше
    float edgeFactor = clamp(edge * 4.0, 0.0, 1.0);
    float anisoX = abs(gx) / (abs(gx)+abs(gy)+0.0001);
    float anisoY = 1.0 - anisoX;

    // Адаптивные веса — вдоль края — узкий фильтр, поперёк — широкий
    vec2 dir = normalize(vec2(gx, gy) + vec2(0.0001));
    // 12-tap kernel с эллиптической формой
    float wCenter = 0.5 + edgeFactor*0.3;
    float wCardinal = 0.12 - edgeFactor*0.05;
    float wDiagonal = 0.06 - edgeFactor*0.02;
    float wFar = 0.02;

    // Учёт анизотропии
    float wx = mix(1.0, 1.0 - anisoX*0.5, edgeFactor);
    float wy = mix(1.0, 1.0 - anisoY*0.5, edgeFactor);

    vec3 result = c * wCenter;
    result += (n*wy + s*wy + e*wx + w*wx) * wCardinal;
    result += (nw + ne + sw + se) * wDiagonal;
    result += (n2*wy + s2*wy + e2*wx + w2*wx) * wFar;

    // Нормализация весов
    float totalW = wCenter + 4.0*wCardinal*0.5*(wx+wy) + 4.0*wDiagonal + 4.0*wFar*0.5*(wx+wy);
    result /= totalW;

    // Лёгкая коррекция — сохраняем детали где edge сильный
    result = mix(result, c, edgeFactor*0.25);

    FragColor = vec4(result, 1.0);
}
)";

// RCAS — Robust Contrast Adaptive Sharpening
static const char* rcasFragSrc = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uImage;
uniform vec2 uTexelSize;
uniform float uSharpness; // 0-1

float luma(vec3 c){ return dot(c, vec3(0.2126,0.7152,0.0722)); }

void main() {
    vec2 uv = vUV;
    vec3 c = texture(uImage, uv).rgb;
    vec3 n = texture(uImage, uv + vec2(0, uTexelSize.y)).rgb;
    vec3 s = texture(uImage, uv - vec2(0, uTexelSize.y)).rgb;
    vec3 e = texture(uImage, uv + vec2(uTexelSize.x, 0)).rgb;
    vec3 w = texture(uImage, uv - vec2(uTexelSize.x, 0)).rgb;

    // Min/max для ограничения overshoot
    vec3 mn = min(min(min(n,s), min(e,w)), c);
    vec3 mx = max(max(max(n,s), max(e,w)), c);

    // Luma для адаптивности
    float lC = luma(c);
    float lN = luma(n), lS = luma(s), lE = luma(e), lW = luma(w);
    float lMin = min(min(min(lN,lS), min(lE,lW)), lC);
    float lMax = max(max(max(lN,lS), max(lE,lW)), lC);
    float contrast = (lMax - lMin) / (lMax + 0.0001);

    // Sharpness адаптивный — меньше шарпа где контраст уже высокий (избегаем ringing)
    float sharp = uSharpness * (1.0 - contrast*0.5);
    sharp = clamp(sharp, 0.0, 1.0);

    // RCAS kernel: 5-tap
    // b = min(0, sharp) ??? упрощённо
    float wCenter = 1.0 + sharp*0.8;
    float wSide = -sharp*0.2;

    vec3 result = c * wCenter + (n+s+e+w) * wSide;

    // Clamp чтобы не выйти за локальный min/max (robust)
    result = clamp(result, mn, mx);

    // Дополнительный ограничитель — не более 5% изменения в тёмных областях
    float lRes = luma(result);
    if (lC < 0.05) {
        result = mix(c, result, 0.5);
    }

    FragColor = vec4(result, 1.0);
}
)";

static unsigned int compileShaderFromSrc(const char* vertSrc, const char* fragSrc) {
    unsigned int vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertSrc, nullptr);
    glCompileShader(vert);
    int ok; glGetShaderiv(vert, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(vert, 1024, nullptr, log);
        std::cerr << "[FSR] Vert compile fail: " << log << std::endl;
        glDeleteShader(vert); return 0;
    }
    unsigned int frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragSrc, nullptr);
    glCompileShader(frag);
    glGetShaderiv(frag, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(frag, 1024, nullptr, log);
        std::cerr << "[FSR] Frag compile fail: " << log << std::endl;
        glDeleteShader(vert); glDeleteShader(frag); return 0;
    }
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(prog, 1024, nullptr, log);
        std::cerr << "[FSR] Link fail: " << log << std::endl;
        glDeleteShader(vert); glDeleteShader(frag); glDeleteProgram(prog); return 0;
    }
    glDeleteShader(vert); glDeleteShader(frag);
    return prog;
}

bool initFSR(int displayW, int displayH) {
    std::cout << "[FSR] Init v1.15.0 — display " << displayW << "x" << displayH << std::endl;

    // Quad VAO
    float quad[] = {
        // pos   uv
        -1, -1,  0, 0,
         1, -1,  1, 0,
         1,  1,  1, 1,
        -1, -1,  0, 0,
         1,  1,  1, 1,
        -1,  1,  0, 1
    };
    if (fsrQuadVAO==0) glGenVertexArrays(1, &fsrQuadVAO);
    if (fsrQuadVBO==0) glGenBuffers(1, &fsrQuadVBO);
    glBindVertexArray(fsrQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, fsrQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Shaders
    fsrEASUProgram = compileShaderFromSrc(quadVertSrc, easuFragSrc);
    fsrRCASProgram = compileShaderFromSrc(quadVertSrc, rcasFragSrc);
    if (fsrEASUProgram==0 || fsrRCASProgram==0) {
        std::cerr << "[FSR] Shader compile failed!" << std::endl;
        return false;
    }

    // FBOs
    resizeFSR(displayW, displayH);

    std::cout << "[FSR] Init OK — EASU=" << fsrEASUProgram << " RCAS=" << fsrRCASProgram << std::endl;
    return true;
}

void shutdownFSR() {
    if (fsrLowResFBO) { glDeleteFramebuffers(1, &fsrLowResFBO); fsrLowResFBO=0; }
    if (fsrLowResColorTex) { glDeleteTextures(1, &fsrLowResColorTex); fsrLowResColorTex=0; }
    if (fsrLowResDepthRBO) { glDeleteRenderbuffers(1, &fsrLowResDepthRBO); fsrLowResDepthRBO=0; }
    if (fsrIntermediateFBO) { glDeleteFramebuffers(1, &fsrIntermediateFBO); fsrIntermediateFBO=0; }
    if (fsrIntermediateTex) { glDeleteTextures(1, &fsrIntermediateTex); fsrIntermediateTex=0; }
    if (fsrEASUProgram) { glDeleteProgram(fsrEASUProgram); fsrEASUProgram=0; }
    if (fsrRCASProgram) { glDeleteProgram(fsrRCASProgram); fsrRCASProgram=0; }
    if (fsrQuadVAO) { glDeleteVertexArrays(1, &fsrQuadVAO); fsrQuadVAO=0; }
    if (fsrQuadVBO) { glDeleteBuffers(1, &fsrQuadVBO); fsrQuadVBO=0; }
}

void resizeFSR(int displayW, int displayH) {
    if (displayW<=0 || displayH<=0) return;

    float scale = fsrEnabled ? fsrCurrentScale : 1.0f;
    if (scale < 0.2f) scale = 0.2f;
    if (scale > 1.0f) scale = 1.0f;
    int lowW = (int)(displayW * scale);
    int lowH = (int)(displayH * scale);
    if (lowW < 64) lowW = 64;
    if (lowH < 64) lowH = 64;
    // Чётные размеры
    lowW = (lowW/2)*2; lowH = (lowH/2)*2;

    // LowRes FBO
    if (fsrLowResFBO==0) glGenFramebuffers(1, &fsrLowResFBO);
    if (fsrLowResColorTex==0) glGenTextures(1, &fsrLowResColorTex);
    if (fsrLowResDepthRBO==0) glGenRenderbuffers(1, &fsrLowResDepthRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, fsrLowResFBO);

    glBindTexture(GL_TEXTURE_2D, fsrLowResColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, lowW, lowH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fsrLowResColorTex, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, fsrLowResDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, lowW, lowH);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fsrLowResDepthRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[FSR] LowRes FBO incomplete!" << std::endl;
    }

    // Intermediate FBO для EASU -> RCAS
    if (fsrIntermediateFBO==0) glGenFramebuffers(1, &fsrIntermediateFBO);
    if (fsrIntermediateTex==0) glGenTextures(1, &fsrIntermediateTex);

    glBindFramebuffer(GL_FRAMEBUFFER, fsrIntermediateFBO);
    glBindTexture(GL_TEXTURE_2D, fsrIntermediateTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, displayW, displayH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fsrIntermediateTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[FSR] Intermediate FBO incomplete!" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cout << "[FSR] Resize: display " << displayW << "x" << displayH << " -> low " << lowW << "x" << lowH << " scale " << scale << std::endl;
}

void beginFSRRender(int displayW, int displayH) {
    if (!fsrEnabled || fsrMode==FSRMode::Off || fsrLowResFBO==0) {
        glViewport(0,0,displayW,displayH);
        return;
    }
    float scale = fsrCurrentScale;
    int lowW = (int)(displayW * scale);
    int lowH = (int)(displayH * scale);
    if (lowW < 64) lowW = 64;
    if (lowH < 64) lowH = 64;
    lowW = (lowW/2)*2; lowH = (lowH/2)*2;

    glBindFramebuffer(GL_FRAMEBUFFER, fsrLowResFBO);
    glViewport(0,0,lowW,lowH);
}

void endFSRRenderAndUpscale(int displayW, int displayH) {
    if (!fsrEnabled || fsrMode==FSRMode::Off || fsrLowResFBO==0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0,0,displayW,displayH);
        return;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    float scale = fsrCurrentScale;
    int lowW = (int)(displayW * scale);
    int lowH = (int)(displayH * scale);
    if (lowW < 64) lowW = 64;
    if (lowH < 64) lowH = 64;
    lowW = (lowW/2)*2; lowH = (lowH/2)*2;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Pass 1: EASU — lowRes -> intermediate (display size)
    glBindFramebuffer(GL_FRAMEBUFFER, fsrIntermediateFBO);
    glViewport(0,0,displayW,displayH);
    glUseProgram(fsrEASUProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fsrLowResColorTex);
    glUniform1i(glGetUniformLocation(fsrEASUProgram, "uLowRes"), 0);
    glUniform2f(glGetUniformLocation(fsrEASUProgram, "uLowResSize"), (float)lowW, (float)lowH);
    glUniform2f(glGetUniformLocation(fsrEASUProgram, "uDisplaySize"), (float)displayW, (float)displayH);
    glUniform2f(glGetUniformLocation(fsrEASUProgram, "uInputViewport"), (float)lowW, (float)lowH);
    glUniform2f(glGetUniformLocation(fsrEASUProgram, "uOutputViewport"), (float)displayW, (float)displayH);
    glBindVertexArray(fsrQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Pass 2: RCAS — intermediate -> backbuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0,0,displayW,displayH);
    if (fsrUseRCAS) {
        glUseProgram(fsrRCASProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fsrIntermediateTex);
        glUniform1i(glGetUniformLocation(fsrRCASProgram, "uImage"), 0);
        glUniform2f(glGetUniformLocation(fsrRCASProgram, "uTexelSize"), 1.0f/displayW, 1.0f/displayH);
        glUniform1f(glGetUniformLocation(fsrRCASProgram, "uSharpness"), fsrSharpness);
        glBindVertexArray(fsrQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    } else {
        // Без RCAS — просто blit intermediate
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fsrIntermediateFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0,0,displayW,displayH, 0,0,displayW,displayH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto t1 = std::chrono::high_resolution_clock::now();
    perfFSRms = std::chrono::duration<float, std::milli>(t1-t0).count();

    // Dynamic resolution — подстраиваем scale под FPS
    if (fsrDynamicRes) {
        float frameMs = perfFrameMs;
        if (frameMs > 0.1f) {
            float currentFPS = 1000.0f / frameMs;
            float diff = currentFPS - fsrTargetFPS;
            // P-контроллер
            float adjust = diff * 0.001f; // 0.001 per FPS diff
            fsrCurrentScale += adjust;
            fsrCurrentScale = std::clamp(fsrCurrentScale, 0.33f, 1.0f);
            // Если FPS сильно ниже — резко снижаем
            if (currentFPS < fsrTargetFPS * 0.8f) fsrCurrentScale -= 0.02f;
            if (currentFPS > fsrTargetFPS * 1.2f) fsrCurrentScale += 0.01f;
            fsrCurrentScale = std::clamp(fsrCurrentScale, 0.33f, 1.0f);
        }
    }
}

// =====================================================
// Frustum culling — 6 planes
// =====================================================
bool isBoxInFrustum(const glm::vec3& minB, const glm::vec3& maxB, const glm::mat4& vp) {
    // Извлекаем 6 плоскостей из VP
    glm::vec4 planes[6];
    // left, right, bottom, top, near, far
    // row-major extraction
    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // left
    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // right
    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // bottom
    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // top
    planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // near
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // far

    for (int i=0;i<6;i++) {
        float len = sqrt(planes[i].x*planes[i].x + planes[i].y*planes[i].y + planes[i].z*planes[i].z);
        if (len>1e-6f) planes[i] /= len;
        // p-vertex — самая дальняя точка бокса по нормали плоскости
        glm::vec3 p = glm::vec3(
            planes[i].x > 0 ? maxB.x : minB.x,
            planes[i].y > 0 ? maxB.y : minB.y,
            planes[i].z > 0 ? maxB.z : minB.z
        );
        if (glm::dot(glm::vec3(planes[i]), p) + planes[i].w < 0) return false;
    }
    return true;
}

int computeLODLevel(float distance, float maxDim) {
    if (!optLOD) return 0;
    float d = distance / (maxDim + 0.001f);
    if (d < 1.5f) return 0; // full
    if (d < 3.0f) return 1; // half
    if (d < 6.0f) return 2; // quarter
    return 3; // culled / lowest
}

bool shouldCullParticle(const glm::vec3& pos, const glm::mat4& vp, int displayW, int displayH) {
    if (!optFrustumCulling) return false;
    glm::vec4 clip = vp * glm::vec4(pos,1.0f);
    if (clip.w <= 0.0f) return true;
    clip /= clip.w;
    // За экраном с небольшим запасом
    if (clip.x < -1.2f || clip.x > 1.2f) return true;
    if (clip.y < -1.2f || clip.y > 1.2f) return true;
    if (clip.z < -1.0f || clip.z > 1.0f) return true;
    return false;
}
