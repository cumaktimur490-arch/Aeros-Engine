#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <vector>
#include <iostream>
#include <cmath>
#include "flow_params.h"

#define CUDA_CHECK(err) do { cudaError_t e=(err); if(e!=cudaSuccess){ \
    std::cerr<<"CUDA "<<__FILE__<<":"<<__LINE__<<": "<<cudaGetErrorString(e)<<std::endl; return; } } while(0)

#define CUDA_CHECK_RET0(err) do { cudaError_t e=(err); if(e!=cudaSuccess){ \
    std::cerr<<"CUDA "<<__FILE__<<":"<<__LINE__<<": "<<cudaGetErrorString(e)<<std::endl; return 0.0f; } } while(0)

__global__ void addOneKernel(float* d, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) d[i] += 1.0f;
}

extern "C" float runCudaTest(const std::vector<float>& in) {
    int n = (int)in.size();
    if (n == 0) return 0.0f;
    float* d;
    CUDA_CHECK_RET0(cudaMalloc(&d, n * sizeof(float)));
    CUDA_CHECK_RET0(cudaMemcpy(d, in.data(), n * sizeof(float), cudaMemcpyHostToDevice));
    int bs = 256, nb = (n + bs - 1) / bs;
    addOneKernel<<<nb, bs>>>(d, n);
    CUDA_CHECK_RET0(cudaGetLastError());
    CUDA_CHECK_RET0(cudaDeviceSynchronize());
    std::vector<float> r(n);
    CUDA_CHECK_RET0(cudaMemcpy(r.data(), d, n * sizeof(float), cudaMemcpyDeviceToHost));
    cudaFree(d);
    float s = 0.0f;
    for (float v : r) s += v;
    return s;
}

// ============ SDF на GPU ============
float* g_dDist = nullptr;
int    d_voxNx = 0, d_voxNy = 0, d_voxNz = 0;
float  d_voxMinX = 0, d_voxMinY = 0, d_voxMinZ = 0;
float  d_cellX = 1, d_cellY = 1, d_cellZ = 1;

// ============ SDF-семплинг ============
__device__ bool sampleSDF(float3 p, float* dist, int nx, int ny, int nz,
                          float mnX, float mnY, float mnZ,
                          float csX, float csY, float csZ, float* outD)
{
    if (!dist) { *outD = 1000.0f; return false; }
    int ix = (int)((p.x - mnX) / csX);
    int iy = (int)((p.y - mnY) / csY);
    int iz = (int)((p.z - mnZ) / csZ);
    if (ix < 0 || ix >= nx || iy < 0 || iy >= ny || iz < 0 || iz >= nz) {
        *outD = 1000.0f;
        return false;
    }
    *outD = dist[(iz*ny + iy)*nx + ix];
    return true;
}

__device__ float3 sdfNormal(float3 p, float* dist, int nx, int ny, int nz,
                            float mnX, float mnY, float mnZ,
                            float csX, float csY, float csZ)
{
    if (!dist) return make_float3(0, 1, 0);
    int ix = (int)((p.x - mnX) / csX);
    int iy = (int)((p.y - mnY) / csY);
    int iz = (int)((p.z - mnZ) / csZ);
    if (ix <= 0 || ix >= nx-1 || iy <= 0 || iy >= ny-1 || iz <= 0 || iz >= nz-1)
        return make_float3(0, 1, 0);

    float dx = dist[(iz*ny + iy)*nx + (ix+1)] - dist[(iz*ny + iy)*nx + (ix-1)];
    float dy = dist[(iz*ny + (iy+1))*nx + ix] - dist[(iz*ny + (iy-1))*nx + ix];
    float dz = dist[((iz+1)*ny + iy)*nx + ix] - dist[((iz-1)*ny + iy)*nx + ix];
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f) return make_float3(0, 1, 0);
    return make_float3(dx/len, dy/len, dz/len);
}

// ============ Поле скоростей на основе SDF ============
__device__ float3 baseFlowSDF(float3 p, FlowParams prm, float* dDist) {
    float3 v = make_float3(prm.vx, prm.vy, prm.vz);

    if (!dDist || prm.gridNx <= 0) return v;

    float d;
    bool ok = sampleSDF(p, dDist, prm.gridNx, prm.gridNy, prm.gridNz,
                       prm.gridMinX, prm.gridMinY, prm.gridMinZ,
                       prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ, &d);
    if (!ok) return v;
    if (d < 0.0f) return make_float3(0.0f, 0.0f, 0.0f);

    float3 n = sdfNormal(p, dDist, prm.gridNx, prm.gridNy, prm.gridNz,
                        prm.gridMinX, prm.gridMinY, prm.gridMinZ,
                        prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ);

    float k = 3.0f * prm.cellSizeX;
    float factor = expf(-d / k);
    if (factor < 1e-4f) return v;

    // Отражение нормальной компоненты
    float vn = v.x*n.x + v.y*n.y + v.z*n.z;
    v.x -= factor * vn * n.x;
    v.y -= factor * vn * n.y;
    v.z -= factor * vn * n.z;

    // Ускорение на боках (эффект Бернулли) — усилено
    if (d < 8.0f * prm.cellSizeX) {
        float boost = expf(-d / (k * 2.0f)) * 0.7f;
        float3 vt = make_float3(v.x, v.y, v.z);
        v.x += boost * vt.x;
        v.y += boost * vt.y;
        v.z += boost * vt.z;
    }

    return v;
}

// ============ Вихревая дорожка ============
__device__ float3 wakeField(float3 p, FlowParams prm) {
    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    float3 w = make_float3(0.0f, 0.0f, 0.0f);
    if (vmag < 1e-4f) return w;

    float dx = prm.vx / vmag, dy = prm.vy / vmag, dz = prm.vz / vmag;
    float rx = p.x - prm.centerX;
    float ry = p.y - prm.centerY;
    float rz = p.z - prm.centerZ;
    float along = rx*dx + ry*dy + rz*dz;
    float px = rx - along*dx, py = ry - along*dy, pz = rz - along*dz;
    float perp = sqrtf(px*px + py*py + pz*pz);
    if (perp < 1e-4f) perp = 1e-4f;

    float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
    if (along < D * 0.5f || along > prm.wakeLength) return w;

    float decay = expf(-(along - D*0.5f) / (prm.wakeLength * 0.4f));
    float width = expf(-perp*perp / (D*D*1.5f));
    float pnx = px/perp, pny = py/perp, pnz = pz/perp;

    float vtx = dy*pnz - dz*pny;
    float vty = dz*pnx - dx*pnz;
    float vtz = dx*pny - dy*pnx;

    float omega = 6.2831853f * prm.strouhal * vmag / D;
    float phase = omega * prm.time - along * 2.0f;
    float amp = prm.wakeStrength * decay * width * vmag;

    w.x += amp * sinf(phase) * vtx;
    w.y += amp * sinf(phase) * vty;
    w.z += amp * sinf(phase) * vtz;

    float lat = amp * 0.5f;
    w.x += lat * cosf(phase) * pnx;
    w.y += lat * cosf(phase) * pny;
    w.z += lat * cosf(phase) * pnz;

    float turb = 0.3f * amp * sinf(prm.time*3.0f + along*3.0f + perp*5.0f);
    w.x += turb;
    w.y += turb * 0.5f;
    w.z += turb * 0.5f;

    return w;
}

__device__ float3 fullField(float3 p, FlowParams prm, float* dDist) {
    float3 v = baseFlowSDF(p, prm, dDist);
    float3 w = wakeField(p, prm);
    return make_float3(v.x + w.x, v.y + w.y, v.z + w.z);
}

// ============ Воксельный запрос ============
__device__ int voxelQuery(float3 p, float* dist, int nx, int ny, int nz,
                          float mnX, float mnY, float mnZ,
                          float csX, float csY, float csZ, float* outDist)
{
    if (!dist) { *outDist = 1000.0f; return 0; }
    int ix = (int)((p.x - mnX) / csX);
    int iy = (int)((p.y - mnY) / csY);
    int iz = (int)((p.z - mnZ) / csZ);
    if (ix < 0 || ix >= nx || iy < 0 || iy >= ny || iz < 0 || iz >= nz) {
        *outDist = 1000.0f;
        return 0;
    }
    int idx = (iz * ny + iy) * nx + ix;
    float d = dist[idx];
    *outDist = d;
    if (d < 0.0f) return 1;
    if (d < 1.5f) return 2;
    return 0;
}

__device__ float3 voxelNormal(float3 p, float* dist, int nx, int ny, int nz,
                              float mnX, float mnY, float mnZ,
                              float csX, float csY, float csZ)
{
    if (!dist) return make_float3(0, 1, 0);
    int ix = (int)((p.x - mnX) / csX);
    int iy = (int)((p.y - mnY) / csY);
    int iz = (int)((p.z - mnZ) / csZ);
    if (ix <= 0 || ix >= nx-1 || iy <= 0 || iy >= ny-1 || iz <= 0 || iz >= nz-1)
        return make_float3(0, 1, 0);

    float dx = dist[(iz*ny + iy)*nx + (ix+1)] - dist[(iz*ny + iy)*nx + (ix-1)];
    float dy = dist[(iz*ny + (iy+1))*nx + ix] - dist[(iz*ny + (iy-1))*nx + ix];
    float dz = dist[((iz+1)*ny + iy)*nx + ix] - dist[((iz-1)*ny + iy)*nx + ix];
    float3 n = make_float3(dx, dy, dz);
    float len = sqrtf(n.x*n.x + n.y*n.y + n.z*n.z);
    if (len < 1e-4f) return make_float3(0, 1, 0);
    return make_float3(n.x/len, n.y/len, n.z/len);
}

// ============ Ядра ============
__global__ void initParticlesKernel(float3* pos, float3* col, int n, FlowParams prm) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    float r1 = fabsf(sinf(i * 12.9898f + 78.233f) * 43758.5453f);
    r1 -= floorf(r1);
    float r2 = fabsf(cosf(i * 39.346f + 11.135f) * 24634.6345f);
    r2 -= floorf(r2);

    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    float3 flowDir = make_float3(1, 0, 0);
    if (vmag > 1e-4f) flowDir = make_float3(prm.vx/vmag, prm.vy/vmag, prm.vz/vmag);

    float offset = 0.8f * (prm.maxX - prm.minX);
    float3 startCenter = make_float3(prm.centerX - flowDir.x * offset,
                                     prm.centerY - flowDir.y * offset,
                                     prm.centerZ - flowDir.z * offset);

    float upRef[3] = {0, 1, 0};
    if (fabsf(flowDir.y) > 0.95f) { upRef[0] = 1; upRef[1] = 0; upRef[2] = 0; }
    float3 right = make_float3(flowDir.y*upRef[2] - flowDir.z*upRef[1],
                               flowDir.z*upRef[0] - flowDir.x*upRef[2],
                               flowDir.x*upRef[1] - flowDir.y*upRef[0]);
    float rl = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
    if (rl > 1e-6f) { right.x/=rl; right.y/=rl; right.z/=rl; }
    float3 up = make_float3(right.y*flowDir.z - right.z*flowDir.y,
                            right.z*flowDir.x - right.x*flowDir.z,
                            right.x*flowDir.y - right.y*flowDir.x);

    int side = (int)sqrtf((float)n) + 1;
    int ix = i % side, iy = i / side;
    float fx = (side <= 1) ? 0.0f : ((float)ix / (side - 1) - 0.5f) * 2.0f;
    float fy = (side <= 1) ? 0.0f : ((float)iy / (side - 1) - 0.5f) * 2.0f;

    float spread = (prm.maxX - prm.minX);
    float x = startCenter.x + right.x*fx*spread + up.x*fy*spread + (r1 - 0.5f) * spread * 0.1f;
    float y = startCenter.y + right.y*fx*spread + up.y*fy*spread + (r2 - 0.5f) * spread * 0.1f;
    float z = startCenter.z + right.z*fx*spread + up.z*fy*spread;

    pos[i] = make_float3(x, y, z);
    col[i] = make_float3(0.3f, 0.8f, 1.0f);
}

__global__ void updateParticlesKernel(float3* pos, float3* col, int n,
                                       FlowParams prm, float dt, float* dDist)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    float3 p = pos[i];
    float3 v = fullField(p, prm, dDist);

    float3 np = make_float3(p.x + v.x * dt * prm.timeScale,
                            p.y + v.y * dt * prm.timeScale,
                            p.z + v.z * dt * prm.timeScale);

    bool collided = false;
    float surfDist = 1000.0f;

    float d;
    bool ok = sampleSDF(np, dDist, prm.gridNx, prm.gridNy, prm.gridNz,
                       prm.gridMinX, prm.gridMinY, prm.gridMinZ,
                       prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ, &d);
    if (ok) surfDist = d;

    if (ok && d < 0.0f) {
        float3 nrm = sdfNormal(np, dDist, prm.gridNx, prm.gridNy, prm.gridNz,
                              prm.gridMinX, prm.gridMinY, prm.gridMinZ,
                              prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ);
        float push = fabsf(d) + 0.5f * prm.cellSizeX;
        np.x += nrm.x * push;
        np.y += nrm.y * push;
        np.z += nrm.z * push;
        collided = true;

        // Улучшенное скольжение вдоль поверхности
        float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
        if (vmag < 1e-4f) vmag = 1e-4f;

        // Нормальная компонента потока
        float vinf_n = prm.vx*nrm.x + prm.vy*nrm.y + prm.vz*nrm.z;
        // Тангенциальная составляющая потока (вдоль поверхности)
        float3 vinf_t = make_float3(prm.vx - vinf_n*nrm.x,
                                    prm.vy - vinf_n*nrm.y,
                                    prm.vz - vinf_n*nrm.z);

        // Убираем нормальную составляющую скорости частицы
        float vn = v.x*nrm.x + v.y*nrm.y + v.z*nrm.z;
        v.x -= vn * nrm.x;
        v.y -= vn * nrm.y;
        v.z -= vn * nrm.z;

        // При ударе под 90° (перпендикулярно) даём сильный тангенциальный импульс
        float perpFactor = fabsf(vinf_n) / (vmag + 1e-6f);
        float slideBoost = 1.2f + 0.8f * perpFactor;

        v.x += vinf_t.x * slideBoost * 0.6f;
        v.y += vinf_t.y * slideBoost * 0.6f;
        v.z += vinf_t.z * slideBoost * 0.6f;

        // Если скорость частицы совсем мала — полностью заменяем на касательный поток
        float spd = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
        if (spd < 0.3f * vmag) {
            v.x = vinf_t.x * slideBoost;
            v.y = vinf_t.y * slideBoost;
            v.z = vinf_t.z * slideBoost;
        }
    } else if (ok && d < 1.5f * prm.cellSizeX) {
        collided = true;
    }

    pos[i] = np;

    float speed = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    float spdT = fminf(speed / (prm.maxSpeed + 1e-6f), 1.0f);

    float3 c;
    if (collided && surfDist < 1.5f * prm.cellSizeX) {
        c = make_float3(1.0f, 0.15f, 0.0f);
    } else if (surfDist < 3.0f * prm.cellSizeX) {
        float blend = (surfDist - 1.5f * prm.cellSizeX) / (1.5f * prm.cellSizeX);
        if (blend < 0.0f) blend = 0.0f;
        if (blend > 1.0f) blend = 1.0f;
        float3 hot = make_float3(1.0f, 0.5f, 0.0f);
        float3 cold;
        if (spdT < 0.5f) cold = make_float3(1.0f, spdT*2.0f, 0.0f);
        else             cold = make_float3(1.0f - (spdT-0.5f)*2.0f, 1.0f, 0.0f);
        c = make_float3(hot.x*blend + cold.x*(1.0f-blend),
                        hot.y*blend + cold.y*(1.0f-blend),
                        hot.z*blend + cold.z*(1.0f-blend));
    } else {
        if (spdT < 0.5f) c = make_float3(1.0f, spdT*2.0f, 0.0f);
        else             c = make_float3(1.0f - (spdT-0.5f)*2.0f, 1.0f, 0.0f);
    }
    col[i] = c;

    if (np.x < prm.minX || np.x > prm.maxX ||
        np.y < prm.minY || np.y > prm.maxY ||
        np.z < prm.minZ || np.z > prm.maxZ)
    {
        float r1 = fabsf(sinf(i * 12.9898f + prm.time * 10.0f) * 43758.5453f);
        r1 -= floorf(r1);
        float r2 = fabsf(cosf(i * 39.346f + prm.time * 15.0f) * 24634.6345f);
        r2 -= floorf(r2);
        pos[i] = make_float3(
            prm.minX + (prm.maxX - prm.minX) * (0.05f + 0.9f * r1),
            prm.minY + (prm.maxY - prm.minY) * (0.05f + 0.9f * r2),
            prm.minZ + 0.05f);
    }
}

// ============ Давление (с эффектом разрежения за кормой) ============
__global__ void pressureKernel(float3* verts, float3* nrms, float3* outCol,
                                int n, FlowParams prm, float* dDist)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    float3 p = verts[i];
    float3 v = fullField(p, prm, dDist);
    float speed = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    float vinf = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    if (vinf < 1e-4f) vinf = 1e-4f;

    float speedRatio = speed / vinf;
    float cp = 1.0f - speedRatio*speedRatio;

    // Разрежение за кормой (wake suction)
    float rx = p.x - prm.centerX;
    float ry = p.y - prm.centerY;
    float rz = p.z - prm.centerZ;
    float fl = 1.0f / vinf;
    float along = rx*(prm.vx*fl) + ry*(prm.vy*fl) + rz*(prm.vz*fl);
    float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
    if (along > D * 0.3f) {
        float w = (along - D*0.3f) / D;
        if (w > 1.0f) w = 1.0f;
        cp -= 0.8f * w * w;
    }
    if (cp > 1.0f) cp = 1.0f;
    if (cp < -3.0f) cp = -3.0f;

    float t = (cp + 1.0f) / 2.0f;
    t = fminf(fmaxf(t, 0.0f), 1.0f);
    float3 c;
    if (t < 0.25f)      { float k = t/0.25f;         c = make_float3(0.0f, k, 1.0f); }
    else if (t < 0.5f)  { float k = (t-0.25f)/0.25f; c = make_float3(0.0f, 1.0f, 1.0f-k); }
    else if (t < 0.75f) { float k = (t-0.5f)/0.25f;  c = make_float3(k, 1.0f, 0.0f); }
    else                { float k = (t-0.75f)/0.25f; c = make_float3(1.0f, 1.0f-k, 0.0f); }
    outCol[i] = c;
}

// ============ Host API ============
extern "C" void setVoxelData(const int* voxel, const float* dist,
                             int nx, int ny, int nz,
                             float mnX, float mnY, float mnZ,
                             float csX, float csY, float csZ)
{
    if (g_dDist) { cudaFree(g_dDist); g_dDist = nullptr; }
    int total = nx * ny * nz;
    if (total <= 0) return;
    if (dist) {
        cudaMalloc(&g_dDist, total * sizeof(float));
        cudaMemcpy(g_dDist, dist, total * sizeof(float), cudaMemcpyHostToDevice);
    }
    d_voxNx = nx; d_voxNy = ny; d_voxNz = nz;
    d_voxMinX = mnX; d_voxMinY = mnY; d_voxMinZ = mnZ;
    d_cellX = csX; d_cellY = csY; d_cellZ = csZ;
}

extern "C" void initParticlesCUDA(std::vector<float>& pos, std::vector<float>& col,
                                   int n, const FlowParams& prm)
{
    pos.resize(n * 3);
    col.resize(n * 3);
    float3 *dp, *dc;
    CUDA_CHECK(cudaMalloc(&dp, n * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&dc, n * sizeof(float3)));
    int bs = 256, nb = (n + bs - 1) / bs;
    initParticlesKernel<<<nb, bs>>>(dp, dc, n, prm);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(pos.data(), dp, n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(col.data(), dc, n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
    cudaFree(dp);
    cudaFree(dc);
}

extern "C" void updateParticlesCUDA(std::vector<float>& pos, std::vector<float>& col,
                                     int n, const FlowParams& prm, float dt)
{
    float3 *dp, *dc;
    CUDA_CHECK(cudaMalloc(&dp, n * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&dc, n * sizeof(float3)));
    CUDA_CHECK(cudaMemcpy(dp, pos.data(), n * 3 * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(dc, col.data(), n * 3 * sizeof(float), cudaMemcpyHostToDevice));
    int bs = 256, nb = (n + bs - 1) / bs;
    updateParticlesKernel<<<nb, bs>>>(dp, dc, n, prm, dt, g_dDist);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(pos.data(), dp, n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(col.data(), dc, n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
    cudaFree(dp);
    cudaFree(dc);
}

extern "C" void computeVertexPressureCUDA(const std::vector<float>& verts,
                                           const std::vector<float>& nrms,
                                           std::vector<float>& outCol,
                                           int n, const FlowParams& prm)
{
    outCol.resize(n * 3);
    float3 *dv, *dn, *dc;
    CUDA_CHECK(cudaMalloc(&dv, n * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&dn, n * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&dc, n * sizeof(float3)));
    CUDA_CHECK(cudaMemcpy(dv, verts.data(), n * 3 * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(dn, nrms.data(), n * 3 * sizeof(float), cudaMemcpyHostToDevice));
    int bs = 256, nb = (n + bs - 1) / bs;
    pressureKernel<<<nb, bs>>>(dv, dn, dc, n, prm, g_dDist);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(outCol.data(), dc, n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
    cudaFree(dv);
    cudaFree(dn);
    cudaFree(dc);
}