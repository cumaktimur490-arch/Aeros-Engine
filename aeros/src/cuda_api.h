#ifndef CUDA_API_H
#define CUDA_API_H
// =====================================================
// Интерфейс CUDA-бэкенда (реализация в kernel.cu)
// =====================================================

#include <vector>

#include "flow_params.h"

extern "C" void initParticlesCUDA(std::vector<float>& positions, std::vector<float>& colors,
                                  int numParticles, const FlowParams& params);
extern "C" void updateParticlesCUDA(std::vector<float>& positions, std::vector<float>& colors,
                                    int numParticles, const FlowParams& params, float dt);
extern "C" void computeVertexPressureCUDA(const std::vector<float>& vertices,
                                          const std::vector<float>& normals,
                                          std::vector<float>& outColors,
                                          int numVertices, const FlowParams& params);
extern "C" void setVoxelData(const int* voxel, const float* dist,
                             int nx, int ny, int nz,
                             float mnX, float mnY, float mnZ,
                             float csX, float csY, float csZ);

#endif // CUDA_API_H
