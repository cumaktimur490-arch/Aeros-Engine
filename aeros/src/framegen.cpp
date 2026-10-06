#include "framegen.h"
#include "globals.h"
#include "flow_field.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>

// =====================================================
// Helpers — v1.17.0 with aerodynamic data
// =====================================================
float getFGScale(int mode) {
    switch ((FGMode)mode) {
        case FGMode::FG_2x: return 2.0f;
        case FGMode::FG_3x: return 3.0f;
        case FGMode::FG_4x: return 4.0f;
        default: return 1.0f;
    }
}
const char* getFGModeName(int mode) {
    switch ((FGMode)mode) {
        case FGMode::Off: return "Off";
        case FGMode::FG_2x: return "2x — 1 gen / 2x FPS";
        case FGMode::FG_3x: return "3x — 2 gen / 3x FPS";
        case FGMode::FG_4x: return "4x — 3 gen / 4x FPS";
        default: return "Unknown";
    }
}
int getFGMultiplier(int mode) {
    switch ((FGMode)mode) {
        case FGMode::FG_2x: return 2;
        case FGMode::FG_3x: return 3;
        case FGMode::FG_4x: return 4;
        default: return 1;
    }
}

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

// Motion vector generation — v1.17.0 Physics Logic Fix — с аэродинамикой
// Использует данные: flowParams, Re, Mach, ground, wake
static const char* motionFragSrc = R"(
#version 330 core
in vec2 vUV;
out vec2 FragMotion;
uniform sampler2D uDepthCurr;
uniform sampler2D uDepthPrev;
uniform mat4 uInvViewProjCurr;
uniform mat4 uViewProjPrev;
uniform mat4 uViewProjCurr;
uniform vec2 uTexelSize;
uniform float uDeltaTime;
uniform vec3 uFlowCenter;
uniform vec3 uFlowRadii;
uniform vec3 uFlowUinf;
uniform vec3 uFlowDir;
uniform float uMaxDim;
uniform float uTime;
uniform float uGroundY;
uniform bool uUseAeroMotion;
uniform bool uGroundEffect;
uniform float uWakeStrength;
uniform float uStrouhal;

// Упрощенный потенциал эллипсоида для motion — как в flow_field.cpp
vec3 ellipsoidPotential(vec3 p, vec3 c, vec3 rad, vec3 Uinf) {
    vec3 d = p - c;
    vec3 dn = d / (rad + vec3(0.000001));
    float r2 = dot(dn, dn);
    if (r2 < 1.0) return -Uinf; // внутри
    if (r2 < 1e-8) return vec3(0.0);
    float r = sqrt(r2);
    float r3 = r2 * r;
    float a3_r3 = 1.0 / r3;
    vec3 Un = Uinf / (rad + vec3(0.000001));
    vec3 rhat = dn / r;
    float UdotR = dot(Un, rhat);
    vec3 upert_n = a3_r3 * (3.0 * UdotR * rhat - Un) * 0.5;
    vec3 upert = upert_n * rad;
    float maxR = max(rad.x, max(rad.y, rad.z));
    float minR = min(rad.x, min(rad.y, rad.z));
    float elong = maxR / (minR + 0.000001);
    float shapeFactor = 1.0 / (1.0 + 0.15 * (elong - 1.0));
    shapeFactor = clamp(shapeFactor, 0.4, 1.0);
    upert *= shapeFactor;
    return upert;
}

vec3 computeFlowAero(vec3 p) {
    vec3 Uinf = uFlowUinf;
    float vInf = length(Uinf);
    if (vInf < 0.0001) return Uinf;
    vec3 flowDir = Uinf / vInf;
    vec3 rad = uFlowRadii;
    vec3 v = Uinf + ellipsoidPotential(p, uFlowCenter, rad, Uinf);

    // Ground mirror с инверсией нормальной — физично
    if (uGroundEffect) {
        float groundY = uGroundY;
        float distGround = p.y - groundY;
        if (distGround < 0.0) return vec3(0.0);
        if (distGround < uMaxDim*0.6) {
            vec3 pMirror = vec3(p.x, groundY - distGround, p.z);
            vec3 vMirror = Uinf + ellipsoidPotential(pMirror, uFlowCenter, rad, Uinf);
            vMirror.y = -vMirror.y; // инверсия для no-penetration
            float mirrorInfluence = exp(-distGround / (uMaxDim*0.18));
            v.x += vMirror.x * mirrorInfluence * 0.3;
            v.z += vMirror.z * mirrorInfluence * 0.3;
            v.y += vMirror.y * mirrorInfluence * 0.5;
            if (distGround < 0.001) v.y = 0.0;
        }
        // Venturi — mass conservation
        if (p.y > groundY && p.y < uFlowCenter.y) {
            float clearance = 0.2; // приближенно, реально из BB
            float Hfar = uMaxDim * 0.6;
            float venturi = Hfar / max(clearance, 0.01);
            venturi = clamp(venturi, 1.0, 1.6);
            // Только под телом
            v *= 1.0 + (venturi-1.0)*0.15;
        }
    }

    // Wake — дефицит + Карман
    float rx = p.x - uFlowCenter.x, ry = p.y - uFlowCenter.y, rz = p.z - uFlowCenter.z;
    float along = dot(vec3(rx,ry,rz), flowDir);
    vec3 rPerpVec = vec3(rx,ry,rz) - flowDir * along;
    float rPerp = length(rPerpVec);
    float D = 2.0 * max(uFlowRadii.y, uFlowRadii.z);
    if (D < 0.0001) D = uMaxDim * 0.5;
    if (along > D*0.2 && along < 8.0) {
        float b = D * 0.22 * sqrt(1.0 + along / D) * (1.0 + 0.08 * along / D);
        b = max(b, 0.04);
        float Cd_est = 0.4;
        float xNorm = max(along / D, 0.15);
        float deficitMag = Cd_est * 0.5 * sqrt(D / (along+0.000001)) * exp(-(rPerp*rPerp)/(b*b));
        deficitMag *= uWakeStrength;
        deficitMag = clamp(deficitMag, 0.0, 0.9);
        v -= Uinf * deficitMag;

        if (rPerp < b*2.5) {
            float st = uStrouhal;
            if (st < 0.000001) st = 0.18;
            float omega = 2.0 * 3.14159265 * st * vInf / D;
            float phase = omega * uTime - along * 0.85;
            float vortexAmp = uWakeStrength * 0.32 * vInf * exp(-rPerp*rPerp/(b*b*1.8)) / sqrt(xNorm);
            vortexAmp = min(vortexAmp, vInf*0.6);
            vec3 radialDir = rPerp > 0.000001 ? rPerpVec / rPerp : vec3(0,1,0);
            vec3 vortexDir = cross(flowDir, radialDir);
            float vLen = length(vortexDir);
            if (vLen > 0.000001) vortexDir /= vLen; else vortexDir = vec3(0,0,1);
            float sinPhase = sin(phase);
            float side = sin(phase*0.55) > 0.0 ? 1.0 : -1.0;
            v += vortexDir * (vortexAmp * sinPhase * side);
        }
    }

    float maxV = vInf * 2.2;
    float curMag2 = dot(v,v);
    if (curMag2 > maxV*maxV) v *= maxV / sqrt(curMag2);
    return v;
}

void main() {
    float depth = texture(uDepthCurr, vUV).r;
    float ndcDepth = depth * 2.0 - 1.0;
    vec4 clip = vec4(vUV * 2.0 - 1.0, ndcDepth, 1.0);
    vec4 worldH = uInvViewProjCurr * clip;
    worldH /= worldH.w;
    vec3 worldPos = worldH.xyz;

    // Аэродинамическое смещение: worldPrev = worldCurr - vFluid*dt
    vec3 aeroOffset = vec3(0.0);
    if (uUseAeroMotion) {
        vec3 vFluid = computeFlowAero(worldPos);
        aeroOffset = vFluid * uDeltaTime;
    }
    vec3 worldPrev = worldPos - aeroOffset;

    vec4 prevClip = uViewProjPrev * vec4(worldPrev, 1.0);
    prevClip /= prevClip.w;
    vec2 prevUV = prevClip.xy * 0.5 + 0.5;
    vec2 motion = vUV - prevUV;

    // Disocclusion по глубине и по большому motion
    float depthPrev = texture(uDepthPrev, prevUV).r;
    float depthDiff = abs(depth - depthPrev);
    if (depthDiff > 0.05) motion = vec2(0.0); // окклюзия
    if (length(motion) > 0.35) motion = vec2(0.0);
    if (depth >= 0.9999) motion = vec2(0.0);

    FragMotion = motion;
}
)";

// Frame interpolation — motion-compensated с аэродинамикой
static const char* interpFragSrc = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uCurrColor;
uniform sampler2D uPrevColor;
uniform sampler2D uMotion;
uniform sampler2D uDepthCurr;
uniform sampler2D uDepthPrev;
uniform float uAlpha;
uniform float uBlendStrength;
uniform vec2 uTexelSize;
uniform bool uUseOpticalFlow;
uniform bool uShowDebug;

float luma(vec3 c){ return dot(c, vec3(0.2126,0.7152,0.0722)); }

void main() {
    vec2 motion = texture(uMotion, vUV).rg;
    float depthCurr = texture(uDepthCurr, vUV).r;
    vec2 prevUV = vUV - motion;
    float depthPrev = texture(uDepthPrev, prevUV).r;
    float depthDiff = abs(depthCurr - depthPrev);
    float occlusion = step(0.02, depthDiff);

    vec2 warpPrevUV = vUV - motion * uAlpha;
    vec2 warpCurrUV = vUV + motion * (1.0 - uAlpha);
    warpPrevUV = clamp(warpPrevUV, vec2(0.0), vec2(1.0));
    warpCurrUV = clamp(warpCurrUV, vec2(0.0), vec2(1.0));

    vec3 colorPrev = texture(uPrevColor, warpPrevUV).rgb;
    vec3 colorCurr = texture(uCurrColor, warpCurrUV).rgb;
    vec3 blended = mix(colorPrev, colorCurr, uAlpha);

    if (occlusion > 0.5) {
        if (uAlpha < 0.5) blended = colorCurr;
        else blended = colorPrev;
        blended = mix(blended, colorCurr, 0.7);
    }

    float motionLen = length(motion);
    if (motionLen < 0.0001) {
        blended = mix(colorPrev, colorCurr, uAlpha);
    }

    vec3 cCurr = texture(uCurrColor, vUV).rgb;
    vec3 n = texture(uCurrColor, vUV + vec2(0, uTexelSize.y)).rgb;
    vec3 s = texture(uCurrColor, vUV - vec2(0, uTexelSize.y)).rgb;
    vec3 e = texture(uCurrColor, vUV + vec2(uTexelSize.x, 0)).rgb;
    vec3 w = texture(uCurrColor, vUV - vec2(uTexelSize.x, 0)).rgb;
    vec3 mn = min(min(min(n,s), min(e,w)), cCurr);
    vec3 mx = max(max(max(n,s), max(e,w)), cCurr);
    mn -= 0.05; mx += 0.05;
    blended = clamp(blended, mn, mx);

    if (uShowDebug) {
        float m = length(motion)*10.0;
        if (occlusion > 0.5) FragColor = vec4(1.0, 0.0, 0.0, 1.0);
        else FragColor = vec4(vec3(m), 1.0);
        return;
    }

    FragColor = vec4(blended, 1.0);
}
)";

static const char* opticalFlowFragSrc = R"(
#version 330 core
in vec2 vUV;
out vec2 FragFlow;
uniform sampler2D uCurrColor;
uniform sampler2D uPrevColor;
uniform vec2 uTexelSize;
float luma(vec3 c){ return dot(c, vec3(0.2126,0.7152,0.0722)); }
void main() {
    vec3 cC = texture(uCurrColor, vUV).rgb;
    vec3 cN = texture(uCurrColor, vUV + vec2(0, uTexelSize.y)).rgb;
    vec3 cS = texture(uCurrColor, vUV - vec2(0, uTexelSize.y)).rgb;
    vec3 cE = texture(uCurrColor, vUV + vec2(uTexelSize.x, 0)).rgb;
    vec3 cW = texture(uCurrColor, vUV - vec2(uTexelSize.x, 0)).rgb;
    float lC = luma(cC), lN = luma(cN), lS = luma(cS), lE = luma(cE), lW = luma(cW);
    float gx = lE - lW;
    float gy = lN - lS;
    vec3 pC = texture(uPrevColor, vUV).rgb;
    float lP = luma(pC);
    float gt = lC - lP;
    float denom = gx*gx + gy*gy + 0.001;
    vec2 flow = -gt * vec2(gx, gy) / denom;
    flow = clamp(flow, vec2(-0.05), vec2(0.05));
    FragFlow = flow;
}
)";

static unsigned int compileShader(const char* vs, const char* fs) {
    unsigned int vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vs, nullptr);
    glCompileShader(vert);
    int ok; glGetShaderiv(vert, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(vert, 1024, nullptr, log);
        std::cerr << "[FG] Vert fail: " << log << std::endl;
        glDeleteShader(vert); return 0;
    }
    unsigned int frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fs, nullptr);
    glCompileShader(frag);
    glGetShaderiv(frag, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(frag, 1024, nullptr, log);
        std::cerr << "[FG] Frag fail: " << log << std::endl;
        glDeleteShader(vert); glDeleteShader(frag); return 0;
    }
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(prog, 1024, nullptr, log);
        std::cerr << "[FG] Link fail: " << log << std::endl;
        glDeleteShader(vert); glDeleteShader(frag); glDeleteProgram(prog); return 0;
    }
    glDeleteShader(vert); glDeleteShader(frag);
    return prog;
}

bool initFrameGen(int displayW, int displayH) {
    std::cout << "[FG] Init v1.17.0 Physics Logic Fix — " << displayW << "x" << displayH << std::endl;

    float quad[] = {
        -1,-1, 0,0,
         1,-1, 1,0,
         1, 1, 1,1,
        -1,-1, 0,0,
         1, 1, 1,1,
        -1, 1, 0,1
    };
    if (fgQuadVAO==0) glGenVertexArrays(1, &fgQuadVAO);
    if (fgQuadVBO==0) glGenBuffers(1, &fgQuadVBO);
    glBindVertexArray(fgQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, fgQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    fgMotionProgram = compileShader(quadVertSrc, motionFragSrc);
    fgInterpProgram = compileShader(quadVertSrc, interpFragSrc);
    fgOpticalFlowProgram = compileShader(quadVertSrc, opticalFlowFragSrc);

    if (fgMotionProgram==0 || fgInterpProgram==0) {
        std::cerr << "[FG] Shader compile failed" << std::endl;
        return false;
    }

    resizeFrameGen(displayW, displayH);
    fgHasHistory = false;
    fgGeneratedCount = 0;
    fgRealCount = 0;

    std::cout << "[FG] Init OK v1.17.0 — motion=" << fgMotionProgram << " interp=" << fgInterpProgram << " aero=ON" << std::endl;
    return true;
}

void shutdownFrameGen() {
    if (fgRealFBO) { glDeleteFramebuffers(1, &fgRealFBO); fgRealFBO=0; }
    if (fgRealColorTex) { glDeleteTextures(1, &fgRealColorTex); fgRealColorTex=0; }
    if (fgRealDepthTex) { glDeleteTextures(1, &fgRealDepthTex); fgRealDepthTex=0; }
    if (fgPrevColorTex) { glDeleteTextures(1, &fgPrevColorTex); fgPrevColorTex=0; }
    if (fgPrevDepthTex) { glDeleteTextures(1, &fgPrevDepthTex); fgPrevDepthTex=0; }
    if (fgMotionFBO) { glDeleteFramebuffers(1, &fgMotionFBO); fgMotionFBO=0; }
    if (fgMotionTex) { glDeleteTextures(1, &fgMotionTex); fgMotionTex=0; }
    if (fgInterpFBO) { glDeleteFramebuffers(1, &fgInterpFBO); fgInterpFBO=0; }
    if (fgInterpTex) { glDeleteTextures(1, &fgInterpTex); fgInterpTex=0; }
    if (fgMotionProgram) { glDeleteProgram(fgMotionProgram); fgMotionProgram=0; }
    if (fgInterpProgram) { glDeleteProgram(fgInterpProgram); fgInterpProgram=0; }
    if (fgOpticalFlowProgram) { glDeleteProgram(fgOpticalFlowProgram); fgOpticalFlowProgram=0; }
    if (fgQuadVAO) { glDeleteVertexArrays(1, &fgQuadVAO); fgQuadVAO=0; }
    if (fgQuadVBO) { glDeleteBuffers(1, &fgQuadVBO); fgQuadVBO=0; }
    fgHasHistory = false;
}

void resizeFrameGen(int displayW, int displayH) {
    if (displayW<=0 || displayH<=0) return;
    int w = displayW, h = displayH;
    w = (w/2)*2; h = (h/2)*2;

    if (fgRealFBO==0) glGenFramebuffers(1, &fgRealFBO);
    if (fgRealColorTex==0) glGenTextures(1, &fgRealColorTex);
    if (fgRealDepthTex==0) glGenTextures(1, &fgRealDepthTex);
    if (fgPrevColorTex==0) glGenTextures(1, &fgPrevColorTex);
    if (fgPrevDepthTex==0) glGenTextures(1, &fgPrevDepthTex);

    glBindFramebuffer(GL_FRAMEBUFFER, fgRealFBO);

    glBindTexture(GL_TEXTURE_2D, fgRealColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fgRealColorTex, 0);

    glBindTexture(GL_TEXTURE_2D, fgRealDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fgRealDepthTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[FG] Real FBO incomplete" << std::endl;
    }

    glBindTexture(GL_TEXTURE_2D, fgPrevColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, fgPrevDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (fgMotionFBO==0) glGenFramebuffers(1, &fgMotionFBO);
    if (fgMotionTex==0) glGenTextures(1, &fgMotionTex);
    glBindFramebuffer(GL_FRAMEBUFFER, fgMotionFBO);
    glBindTexture(GL_TEXTURE_2D, fgMotionTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, w, h, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fgMotionTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[FG] Motion FBO incomplete" << std::endl;
    }

    if (fgInterpFBO==0) glGenFramebuffers(1, &fgInterpFBO);
    if (fgInterpTex==0) glGenTextures(1, &fgInterpTex);
    glBindFramebuffer(GL_FRAMEBUFFER, fgInterpFBO);
    glBindTexture(GL_TEXTURE_2D, fgInterpTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fgInterpTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[FG] Interp FBO incomplete" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cout << "[FG] Resize v1.17.0 " << w << "x" << h << " aero=ON" << std::endl;
}

void beginRealFrame(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos) {
    fgCurrView = view;
    fgCurrProj = proj;
    fgCurrCameraPos = camPos;
}

void endRealFrame() {}

bool hasFGHistory() { return fgHasHistory; }

void fgBindRealFBO() {
    if (fgRealFBO) glBindFramebuffer(GL_FRAMEBUFFER, fgRealFBO);
}
void fgUnbindFBO(int displayW, int displayH) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0,0,displayW,displayH);
}
unsigned int fgGetRealColorTex() { return fgRealColorTex; }

void updateFGHistory(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos) {
    fgPrevView = fgCurrView;
    fgPrevProj = fgCurrProj;
    fgPrevCameraPos = fgCurrCameraPos;
    if (fgRealColorTex && fgPrevColorTex && fgRealDepthTex && fgPrevDepthTex) {
        if (GLAD_GL_VERSION_4_3) {
            glCopyImageSubData(fgRealColorTex, GL_TEXTURE_2D, 0,0,0,0,
                               fgPrevColorTex, GL_TEXTURE_2D, 0,0,0,0,
                               display_w, display_h, 1);
            glCopyImageSubData(fgRealDepthTex, GL_TEXTURE_2D, 0,0,0,0,
                               fgPrevDepthTex, GL_TEXTURE_2D, 0,0,0,0,
                               display_w, display_h, 1);
        } else {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fgRealFBO);
            glBindTexture(GL_TEXTURE_2D, fgPrevColorTex);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0,0, 0,0, display_w, display_h);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
    fgHasHistory = true;
    fgRealCount++;
}

void generateInterpolatedFrame(float alpha, int displayW, int displayH) {
    if (!fgHasHistory || !fgEnabled) return;
    auto t0 = std::chrono::high_resolution_clock::now();

    {
        auto tm0 = std::chrono::high_resolution_clock::now();
        glBindFramebuffer(GL_FRAMEBUFFER, fgMotionFBO);
        glViewport(0,0,displayW,displayH);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(fgMotionProgram);

        glm::mat4 currVP = fgCurrProj * fgCurrView;
        glm::mat4 prevVP = fgPrevProj * fgPrevView;
        glm::mat4 invCurrVP = glm::inverse(currVP);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fgRealDepthTex);
        glUniform1i(glGetUniformLocation(fgMotionProgram, "uDepthCurr"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, fgPrevDepthTex);
        glUniform1i(glGetUniformLocation(fgMotionProgram, "uDepthPrev"), 1);

        glUniformMatrix4fv(glGetUniformLocation(fgMotionProgram, "uInvViewProjCurr"), 1, GL_FALSE, &invCurrVP[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(fgMotionProgram, "uViewProjPrev"), 1, GL_FALSE, &prevVP[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(fgMotionProgram, "uViewProjCurr"), 1, GL_FALSE, &currVP[0][0]);
        glUniform2f(glGetUniformLocation(fgMotionProgram, "uTexelSize"), 1.0f/displayW, 1.0f/displayH);
        glUniform1f(glGetUniformLocation(fgMotionProgram, "uDeltaTime"), deltaTime);
        glUniform3f(glGetUniformLocation(fgMotionProgram, "uFlowCenter"), flowParams.centerX, flowParams.centerY, flowParams.centerZ);
        glUniform3f(glGetUniformLocation(fgMotionProgram, "uFlowRadii"), flowParams.radiusX, flowParams.radiusY, flowParams.radiusZ);
        glUniform3f(glGetUniformLocation(fgMotionProgram, "uFlowUinf"), flowParams.vx, flowParams.vy, flowParams.vz);
        glm::vec3 flowDir = glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
        float fl = glm::length(flowDir);
        if (fl > 1e-6f) flowDir /= fl; else flowDir = glm::vec3(1,0,0);
        glUniform3f(glGetUniformLocation(fgMotionProgram, "uFlowDir"), flowDir.x, flowDir.y, flowDir.z);
        glUniform1f(glGetUniformLocation(fgMotionProgram, "uMaxDim"), maxDim);
        glUniform1f(glGetUniformLocation(fgMotionProgram, "uTime"), flowParams.time);
        float groundY = g_voxMinY + aeroGroundHeight;
        glUniform1f(glGetUniformLocation(fgMotionProgram, "uGroundY"), groundY);
        glUniform1i(glGetUniformLocation(fgMotionProgram, "uUseAeroMotion"), fgUseMotionVectors ? 1 : 0);
        glUniform1i(glGetUniformLocation(fgMotionProgram, "uGroundEffect"), aeroGroundEffect ? 1 : 0);
        glUniform1f(glGetUniformLocation(fgMotionProgram, "uWakeStrength"), flowParams.wakeStrength);
        glUniform1f(glGetUniformLocation(fgMotionProgram, "uStrouhal"), flowParams.strouhal);

        glBindVertexArray(fgQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        auto tm1 = std::chrono::high_resolution_clock::now();
        perfMotionMs = std::chrono::duration<float, std::milli>(tm1-tm0).count();
    }

    {
        auto ti0 = std::chrono::high_resolution_clock::now();
        glBindFramebuffer(GL_FRAMEBUFFER, fgInterpFBO);
        glViewport(0,0,displayW,displayH);
        glUseProgram(fgInterpProgram);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fgRealColorTex);
        glUniform1i(glGetUniformLocation(fgInterpProgram, "uCurrColor"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, fgPrevColorTex);
        glUniform1i(glGetUniformLocation(fgInterpProgram, "uPrevColor"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, fgMotionTex);
        glUniform1i(glGetUniformLocation(fgInterpProgram, "uMotion"), 2);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, fgRealDepthTex);
        glUniform1i(glGetUniformLocation(fgInterpProgram, "uDepthCurr"), 3);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, fgPrevDepthTex);
        glUniform1i(glGetUniformLocation(fgInterpProgram, "uDepthPrev"), 4);

        glUniform1f(glGetUniformLocation(fgInterpProgram, "uAlpha"), alpha);
        glUniform1f(glGetUniformLocation(fgInterpProgram, "uBlendStrength"), fgBlendStrength);
        glUniform2f(glGetUniformLocation(fgInterpProgram, "uTexelSize"), 1.0f/displayW, 1.0f/displayH);
        glUniform1i(glGetUniformLocation(fgInterpProgram, "uUseOpticalFlow"), fgUseOpticalFlow ? 1 : 0);
        glUniform1i(glGetUniformLocation(fgInterpProgram, "uShowDebug"), fgShowDebug ? 1 : 0);

        glBindVertexArray(fgQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        auto ti1 = std::chrono::high_resolution_clock::now();
        perfInterpMs = std::chrono::duration<float, std::milli>(ti1-ti0).count();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);

    auto t1 = std::chrono::high_resolution_clock::now();
    perfFGms = std::chrono::duration<float, std::milli>(t1-t0).count() + perfMotionMs + perfInterpMs;
}

void presentInterpolatedFrame(int displayW, int displayH) {
    if (!fgInterpTex) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0,0,displayW,displayH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fgInterpFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,displayW,displayH, 0,0,displayW,displayH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    fgGeneratedCount++;
}

void presentRealFrame(int displayW, int displayH) {
    if (!fgRealColorTex) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0,0,displayW,displayH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fgRealFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,displayW,displayH, 0,0,displayW,displayH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
}
