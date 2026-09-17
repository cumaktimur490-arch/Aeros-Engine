#ifndef VOXEL_GRID_H
#define VOXEL_GRID_H
// =====================================================
// Вокселизация модели и поле расстояний (SDF)
// =====================================================

#include <glm/glm.hpp>
#include <vector>

void buildVoxelGrid(const std::vector<float>& verts, int res);
float sampleSDFCPU(const glm::vec3& p);
glm::vec3 sdfNormalCPU(const glm::vec3& p);

#endif // VOXEL_GRID_H
