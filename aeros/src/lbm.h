#ifndef LBM_H
#define LBM_H
// =====================================================
// LBM solver (D3Q19) on CUDA - real aerodynamics
// over the voxel grid and SDF
// =====================================================

#include <vector>

#include "flow_params.h"

void initLBM();
bool lbmReady();

void stepLBM(int substeps, const FlowParams& prm, float nuLattice,
             float* outDragCoef, float* outLiftCoef);

void updateParticlesLBM(std::vector<float>& pos, std::vector<float>& col,
                        int n, const FlowParams& prm, float dt);

void surfacePressureLBM(const std::vector<float>& verts, std::vector<float>& outCol,
                        int n, const FlowParams& prm);

void releaseLBM();

#endif // LBM_H