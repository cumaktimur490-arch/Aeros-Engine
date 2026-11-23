// Сборка GLSL-шейдеров Vulkan в SPIR-V и генерация aeros/src/vulkan_scene_spv.h.
//
//   npm install @webgpu/glslang      (один раз, только для разработчика)
//   node tools/build-shaders.js
//
// Зачем: движок не тащит в поставку glslc и .spv-файлы рядом с exe — шейдеры
// вкомпилированы в исполняемый файл как массивы uint32_t. Пересобирать нужно
// только после правки shaders/*.vert|frag.
const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const glslangModule = require(path.join(root, "node_modules", "@webgpu", "glslang",
                                       "dist", "node-devel", "glslang"));

function emitArray(name, words) {
    let out = `static const uint32_t ${name}[] = {\n`;
    for (let i = 0; i < words.length; i += 8) {
        // Array.from обязателен: map по Uint32Array вернул бы типизированный
        // массив и превратил строки в нули — файл получился бы из нулей.
        out += "    " + Array.from(words.slice(i, i + 8))
            .map(w => "0x" + (w >>> 0).toString(16).padStart(8, "0") + "u").join(", ") + ",\n";
    }
    return out + "};\n";
}

glslangModule().then(g => {
    const stages = [
        ["shaders/scene.vert", "vertex", "kSceneVertSpv"],
        ["shaders/scene.frag", "fragment", "kSceneFragSpv"],
    ];
    let body = "";
    let meta = "";
    for (const [file, stage, name] of stages) {
        const src = fs.readFileSync(path.join(root, file), "utf8");
        const spv = g.compileGLSL(src, stage);
        body += emitArray(name, spv);
        meta += `${file} -> ${stage}: ${spv.length} слов\n`;
        console.log(`${file} (${stage}): ${spv.length} слов SPIR-V`);
    }
    const header = `#pragma once
// =====================================================
// Aeros Engine — сгенерированный файл, не править вручную.
// Источник: shaders/scene.vert, shaders/scene.frag
// Сборка: node tools/build-shaders.js (glslang, WASM)
//
${meta.replace(/^/gm, "//   ")}// Шейдеры вкомпилированы в двоичный файл: рядом с exe не нужны ни .spv, ни glslc.
// =====================================================

#include <cstdint>

${body}
static const uint32_t kSceneVertSpvSize = sizeof(kSceneVertSpv) / sizeof(kSceneVertSpv[0]);
static const uint32_t kSceneFragSpvSize = sizeof(kSceneFragSpv) / sizeof(kSceneFragSpv[0]);
`;
    const outPath = path.join(root, "aeros", "src", "vulkan_scene_spv.h");
    fs.writeFileSync(outPath, header);
    console.log("записано:", outPath);
});
