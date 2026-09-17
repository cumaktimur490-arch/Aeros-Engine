// =====================================================
// LBM D3Q19 (lattice Boltzmann) na CUDA.
// Realnoe pole skorostej i davlenie poverh vokselnoj setki:
//  - stolknovenie BGK + model Smagorinskogo
//  - bounce-back na stenkah (SDF < 0), ravnovesnyj vhod,
//    zero-gradient na vyhode/bokah
//  - sily cherez obmen impulsom (momentum exchange) -> Cd/Cl
// Algoritm validirovan CPU-prototipom: Cp(stagnation)=1.0,
// Cd sfery ~0.8 pri Re=280.
// =====================================================

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <glm/glm.hpp>

#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <iostream>

#include "globals.h"
#include "flow_params.h"
#include "lbm.h"

// ---- D3Q19 ----
static const int Q = 19;
__constant__ int   c_dirs[Q*3];
__constant__ int   c_opp[Q];
__constant__ float c_w[Q];

static const int H_DIRS[Q*3] = {
    0,0,0,  1,0,0, -1,0,0,  0,1,0, 0,-1,0,  0,0,1, 0,0,-1,
    1,1,0, -1,-1,0,  1,-1,0, -1,1,0,
    1,0,1, -1,0,-1,  1,0,-1, -1,0,1,
    0,1,1,  0,-1,-1, 0,1,-1, 0,-1,1 };
static const int H_OPP[Q] = {0,2,1,4,3,6,5,8,7,10,9,12,11,14,13,16,15,18,17};
static const float H_W[Q] = {
    1.f/3.f,
    1.f/18.f,1.f/18.f,1.f/18.f,1.f/18.f,1.f/18.f,1.f/18.f,
    1.f/36.f,1.f/36.f,1.f/36.f,1.f/36.f,1.f/36.f,
    1.f/36.f,1.f/36.f,1.f/36.f,1.f/36.f,
    1.f/36.f,1.f/36.f,1.f/36.f };

#define CUDA_CHECK(err) do { cudaError_t e=(err); if(e!=cudaSuccess){ \
    std::cerr<<"LBM CUDA "<<__FILE__<<":"<<__LINE__<<": "<<cudaGetErrorString(e)<<std::endl; return; } } while(0)

// ---- sostoyanie ustrojstva ----
static float* d_fA = nullptr;   // raspredeleniya (19 * cells)
static float* d_fB = nullptr;
static float* d_rho = nullptr;
static float* d_ux = nullptr;
static float* d_uy = nullptr;
static float* d_uz = nullptr;
static float* d_fSum = nullptr; // [3] summarnaya sila

// chasticy (persistentnye device-bufery)
static float* d_ppos = nullptr;
static float* d_pcol = nullptr;
static int    d_pcap = 0;

// ---- sostoyanie hosta ----
static int   h_nx=0, h_ny=0, h_nz=0, h_cells=0;
static bool  s_ready = false;
static float g_Ulat = 0.1f;     // reshetochuya skorost nabegayushchego potoka

// SDF-pole zagruzhaet kernel.cu (setVoxelData)
extern "C" float* getCudaDistField();

// =====================================================
// Device-helpers (SDF)
// =====================================================
__device__ float lbmSampleDist(const float* dist, int nx, int ny, int nz,
                               float x, float y, float z,
                               float mnX, float mnY, float mnZ,
                               float csX, float csY, float csZ)
{
    int ix = (int)((x - mnX) / csX);
    int iy = (int)((y - mnY) / csY);
    int iz = (int)((z - mnZ) / csZ);
    if (ix < 0 || ix >= nx || iy < 0 || iy >= ny || iz < 0 || iz >= nz) return 1000.0f;
    return dist[(iz*ny + iy)*nx + ix];   // v vokseleyah
}

__device__ float3 lbmDistNormal(const float* dist, int nx, int ny, int nz,
                                float x, float y, float z,
                                float mnX, float mnY, float mnZ,
                                float csX, float csY, float csZ)
{
    int ix = (int)((x - mnX) / csX);
    int iy = (int)((y - mnY) / csY);
    int iz = (int)((z - mnZ) / csZ);
    if (ix <= 0 || ix >= nx-1 || iy <= 0 || iy >= ny-1 || iz <= 0 || iz >= nz-1)
        return make_float3(0.f, 1.f, 0.f);
    float dx = dist[(iz*ny+iy)*nx + (ix+1)] - dist[(iz*ny+iy)*nx + (ix-1)];
    float dy = dist[(iz*ny+(iy+1))*nx + ix] - dist[(iz*ny+(iy-1))*nx + ix];
    float dz = dist[((iz+1)*ny+iy)*nx + ix] - dist[((iz-1)*ny+iy)*nx + ix];
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f) return make_float3(0.f, 1.f, 0.f);
    return make_float3(dx/len, dy/len, dz/len);
}

// =====================================================
// Inicializaciya: f = feq(rho0, uIn) vo vsej zhidkosti
// =====================================================
__device__ inline void feqOne(int q, float r, float ux, float uy, float uz, float* fe) {
    float cu = 3.f*(c_dirs[q*3+0]*ux + c_dirs[q*3+1]*uy + c_dirs[q*3+2]*uz);
    float u2 = 1.5f*(ux*ux + uy*uy + uz*uz);
    fe[q] = c_w[q]*r*(1.f + cu + 0.5f*cu*cu - u2);
}

__device__ inline void feqAll(float* fe, float r, float ux, float uy, float uz) {
    for (int q = 0; q < Q; q++) feqOne(q, r, ux, uy, uz, fe);
}

__global__ void lbmInitKernel(float* f, float* rho, float* ux, float* uy, float* uz,
                              const float* dist, int nx, int ny, int nz, float3 uIn)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int cells = nx*ny*nz;
    if (idx >= cells) return;
    if (dist[idx] < 0.0f) { rho[idx]=0; ux[idx]=0; uy[idx]=0; uz[idx]=0; return; }
    float fe[Q];
    feqAll(fe, 1.0f, uIn.x, uIn.y, uIn.z);
    for (int q = 0; q < Q; q++) f[idx*Q+q] = fe[q];
    rho[idx] = 1.0f; ux[idx]=uIn.x; uy[idx]=uIn.y; uz[idx]=uIn.z;
}

// =====================================================
// Stolknovenie + streaming (push) + bounce-back + sily
// =====================================================
__global__ void collideStreamKernel(const float* __restrict__ fIn, float* __restrict__ fOut,
                                    float* rho, float* ux, float* uy, float* uz,
                                    const float* __restrict__ dist,
                                    int nx, int ny, int nz,
                                    float tau0, float smagC, float3 uIn, float* fSum)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int cells = nx*ny*nz;
    if (idx >= cells) return;
    if (dist[idx] < 0.0f) return;   // tverdye yachejki ne uchastvuyut

    float f0[Q];
    float r = 0.f, mx = 0.f, my = 0.f, mz = 0.f;
    #pragma unroll
    for (int q = 0; q < Q; q++) {
        float v = fIn[idx*Q+q];
        f0[q] = v;
        r  += v;
        mx += c_dirs[q*3+0]*v;
        my += c_dirs[q*3+1]*v;
        mz += c_dirs[q*3+2]*v;
    }
    float invr = 1.0f / fmaxf(r, 1e-6f);
    float vx = mx*invr, vy = my*invr, vz = mz*invr;
    rho[idx] = r; ux[idx]=vx; uy[idx]=vy; uz[idx]=vz;

    float fe[Q];
    feqAll(fe, r, vx, vy, vz);

    // Smagorinskij: effektivnoe vremya relaxacii
    float pxx=0,pyy=0,pzz=0,pxy=0,pxz=0,pyz=0;
    #pragma unroll
    for (int q = 0; q < Q; q++) {
        float d = f0[q] - fe[q];
        pxx += c_dirs[q*3+0]*c_dirs[q*3+0]*d;
        pyy += c_dirs[q*3+1]*c_dirs[q*3+1]*d;
        pzz += c_dirs[q*3+2]*c_dirs[q*3+2]*d;
        pxy += c_dirs[q*3+0]*c_dirs[q*3+1]*d;
        pxz += c_dirs[q*3+0]*c_dirs[q*3+2]*d;
        pyz += c_dirs[q*3+1]*c_dirs[q*3+2]*d;
    }
    float Qs = sqrtf(2.f*(pxx*pxx + pyy*pyy + pzz*pzz + 2.f*(pxy*pxy + pxz*pxz + pyz*pyz)));
    float tau = 0.5f*(tau0 + sqrtf(tau0*tau0 + 18.f*smagC*smagC*Qs*invr));

    int x = idx % nx;
    int y = (idx / nx) % ny;
    int z = idx / (nx * ny);

    #pragma unroll
    for (int q = 0; q < Q; q++) {
        float fc = f0[q] - (f0[q] - fe[q]) / tau;
        int tx = x + c_dirs[q*3+0];
        int ty = y + c_dirs[q*3+1];
        int tz = z + c_dirs[q*3+2];
        if (tx < 0 || tx >= nx || ty < 0 || ty >= ny || tz < 0 || tz >= nz) continue;
        int tidx = (tz*ny + ty)*nx + tx;
        if (dist[tidx] < 0.0f) {
            // bounce-back: otrazhaem v protivopolozhnoe napravlenie
            fOut[idx*Q + c_opp[q]] = fc;
            // obmen impulsom: sila na telo
            atomicAdd(&fSum[0], 2.f*fc*c_dirs[q*3+0]);
            atomicAdd(&fSum[1], 2.f*fc*c_dirs[q*3+1]);
            atomicAdd(&fSum[2], 2.f*fc*c_dirs[q*3+2]);
        } else {
            fOut[tidx*Q + q] = fc;
        }
    }
}
__global__ void boundaryKernel(float* f, const float* __restrict__ dist,
                               int nx, int ny, int nz,
                               float3 fd, float3 uIn)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int cells = nx*ny*nz;
    if (idx >= cells) return;
    if (dist[idx] < 0.0f) return;

    int x = idx % nx;
    int y = (idx / nx) % ny;
    int z = idx / (nx * ny);

    // Veter duet V napravlenie fd.Gran vhodnaya, esli fd napravlen VNUTR domena.
    // Inache - vyhod/bokovaya: zero-gradient na unknown-sloty.
    float fe[Q];
    if (x == 0) {
        if (fd.x > 0.05f) {
            feqAll(fe, 1.0f, uIn.x, uIn.y, uIn.z);
            for (int q = 0; q < Q; q++) f[idx*Q+q] = fe[q];
        } else {
            int sidx = (z*ny + y)*nx + 1;
            for (int q = 0; q < Q; q++)
                if (c_dirs[q*3+0] > 0) f[idx*Q+q] = f[sidx*Q+q];
        }
    }
    if (x == nx-1) {
        if (fd.x < -0.05f) {
            feqAll(fe, 1.0f, uIn.x, uIn.y, uIn.z);
            for (int q = 0; q < Q; q++) f[idx*Q+q] = fe[q];
        } else {
            int sidx = (z*ny + y)*nx + (nx-2);
            for (int q = 0; q < Q; q++)
                if (c_dirs[q*3+0] < 0) f[idx*Q+q] = f[sidx*Q+q];
        }
    }
    if (y == 0) {
        if (fd.y > 0.05f) {
            feqAll(fe, 1.0f, uIn.x, uIn.y, uIn.z);
            for (int q = 0; q < Q; q++) f[idx*Q+q] = fe[q];
        } else {
            int sidx = (z*ny + 1)*nx + x;
            for (int q = 0; q < Q; q++)
                if (c_dirs[q*3+1] > 0) f[idx*Q+q] = f[sidx*Q+q];
        }
    }
    if (y == ny-1) {
        if (fd.y < -0.05f) {
            feqAll(fe, 1.0f, uIn.x, uIn.y, uIn.z);
            for (int q = 0; q < Q; q++) f[idx*Q+q] = fe[q];
        } else {
            int sidx = (z*ny + (ny-2))*nx + x;
            for (int q = 0; q < Q; q++)
                if (c_dirs[q*3+1] < 0) f[idx*Q+q] = f[sidx*Q+q];
        }
    }
    if (z == 0) {
        if (fd.z > 0.05f) {
            feqAll(fe, 1.0f, uIn.x, uIn.y, uIn.z);
            for (int q = 0; q < Q; q++) f[idx*Q+q] = fe[q];
        } else {
            int sidx = ((1)*ny + y)*nx + x;
            for (int q = 0; q < Q; q++)
                if (c_dirs[q*3+2] > 0) f[idx*Q+q] = f[sidx*Q+q];
        }
    }
    if (z == nz-1) {
        if (fd.z < -0.05f) {
            feqAll(fe, 1.0f, uIn.x, uIn.y, uIn.z);
            for (int q = 0; q < Q; q++) f[idx*Q+q] = fe[q];
        } else {
            int sidx = ((nz-2)*ny + y)*nx + x;
            for (int q = 0; q < Q; q++)
                if (c_dirs[q*3+2] < 0) f[idx*Q+q] = f[sidx*Q+q];
        }
    }
}

// =====================================================
// Chasticy: advekciya po polyu LBM
// =====================================================
__device__ inline float trilinear(const float* arr, int nx, int ny, int nz,
                                  float x, float y, float z,
                                  float mnX, float mnY, float mnZ,
                                  float csX, float csY, float csZ)
{
    float gx = (x - mnX)/csX - 0.5f;
    float gy = (y - mnY)/csY - 0.5f;
    float gz = (z - mnZ)/csZ - 0.5f;
    int i0 = (int)floorf(gx), j0 = (int)floorf(gy), k0 = (int)floorf(gz);
    float tx = gx - i0, ty = gy - j0, tz = gz - k0;
    float sum = 0.f;
    for (int c = 0; c < 8; c++) {
        int i = i0 + (c     & 1), j = j0 + ((c>>1) & 1), k = k0 + ((c>>2) & 1);
        if (i < 0) i = 0; if (j < 0) j = 0; if (k < 0) k = 0;
        if (i >= nx) i = nx-1; if (j >= ny) j = ny-1; if (k >= nz) k = nz-1;
        float w = ((c     & 1) ? tx : 1.f-tx) *
                  (((c>>1) & 1) ? ty : 1.f-ty) *
                  (((c>>2) & 1) ? tz : 1.f-tz);
        sum += w * arr[(k*ny + j)*nx + i];
    }
    return sum;
}

__device__ inline float hash01(float a, float b) {
    float v = fabsf(sinf(a*12.9898f + b*78.233f) * 43758.5453f);
    return v - floorf(v);
}

__global__ void advectParticlesKernel(float3* pos, float3* col, int n,
                                      const float* __restrict__ dist,
                                      const float* __restrict__ vxA,
                                      const float* __restrict__ vyA,
                                      const float* __restrict__ vzA,
                                      int nx, int ny, int nz,
                                      float mnX, float mnY, float mnZ,
                                      float csX, float csY, float csZ,
                                      float3 flowDirW, float flowSpeed,
                                      float Ulat, float dtEff,
                                      float3 minB, float3 maxB,
                                      float3 spawnCenter, float3 spawnRight,
                                      float3 spawnUp, float spreadR, float spreadU,
                                      float time, float maxSpeedRef, float cell)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    float3 p = pos[i];
    float uxv = trilinear(vxA, nx,ny,nz, p.x,p.y,p.z, mnX,mnY,mnZ, csX,csY,csZ);
    float uyv = trilinear(vyA, nx,ny,nz, p.x,p.y,p.z, mnX,mnY,mnZ, csX,csY,csZ);
    float uzv = trilinear(vzA, nx,ny,nz, p.x,p.y,p.z, mnX,mnY,mnZ, csX,csY,csZ);

    // reshetochuya skorost -> mirovaya
    float scale = (Ulat > 1e-5f) ? (flowSpeed / Ulat) : 0.f;
    float3 v = make_float3(uxv*scale, uyv*scale, uzv*scale);
    float3 np = make_float3(p.x + v.x*dtEff, p.y + v.y*dtEff, p.z + v.z*dtEff);

    // stolknovenie s telom: vytalkivaem po normali SDF
    float surfDist = lbmSampleDist(dist, nx,ny,nz, np.x,np.y,np.z, mnX,mnY,mnZ, csX,csY,csZ);
    bool nearWall = false;
    if (surfDist < 0.0f) {
        float3 nrm = lbmDistNormal(dist, nx,ny,nz, np.x,np.y,np.z, mnX,mnY,mnZ, csX,csY,csZ);
        float push = fabsf(surfDist)*csX + 0.5f*csX;
        np.x += nrm.x*push; np.y += nrm.y*push; np.z += nrm.z*push;
        nearWall = true;
    } else if (surfDist < 1.5f*csX) {
        nearWall = true;
    }

    // vyshli za dom - respawn na vhodnoj ploskosti
    if (np.x < minB.x || np.x > maxB.x ||
        np.y < minB.y || np.y > maxB.y ||
        np.z < minB.z || np.z > maxB.z) {
        float r1 = hash01((float)i, time);
        float r2 = hash01((float)i*1.7f + 11.f, time*1.3f + 3.f);
        np.x = spawnCenter.x + spawnRight.x*(r1-0.5f)*spreadR + spawnUp.x*(r2-0.5f)*spreadU;
        np.y = spawnCenter.y + spawnRight.y*(r1-0.5f)*spreadR + spawnUp.y*(r2-0.5f)*spreadU;
        np.z = spawnCenter.z + spawnRight.z*(r1-0.5f)*spreadR + spawnUp.z*(r2-0.5f)*spreadU;
        nearWall = false;
    }

    pos[i] = np;

    // cvet: skorost + blizost k poverhnosti
    float spd = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    float spdT = spd / (maxSpeedRef + 1e-5f);
    if (spdT > 1.f) spdT = 1.f;
    float3 c;
    if (nearWall) {
        c = make_float3(1.0f, 0.15f, 0.0f);
    } else if (spdT < 0.5f) {
        c = make_float3(1.0f, spdT*2.0f, 0.0f);
    } else {
        c = make_float3(1.0f - (spdT-0.5f)*2.0f, 1.0f, 0.0f);
    }
    col[i] = c;
}

// =====================================================
// Davlenie na poverhnosti: Cp iz plotnosti reshetki
// =====================================================
__global__ void surfacePressureKernel(const float3* verts, float3* outCol, int n,
                                      const float* __restrict__ dist,
                                      const float* __restrict__ rhoA,
                                      int nx, int ny, int nz,
                                      float mnX, float mnY, float mnZ,
                                      float csX, float csY, float csZ,
                                      float Ulat)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    float3 p = verts[i];

    float d0 = lbmSampleDist(dist, nx,ny,nz, p.x,p.y,p.z, mnX,mnY,mnZ, csX,csY,csZ);
    float3 nrm = (d0 < 500.f)
        ? lbmDistNormal(dist, nx,ny,nz, p.x,p.y,p.z, mnX,mnY,mnZ, csX,csY,csZ)
        : make_float3(0.f,1.f,0.f);

    // smeshchenie v zhidkost, chtoby sempl ne popal v tverduyu yachejku
    float off = 1.6f*csX;
    float3 ps = make_float3(p.x + nrm.x*off, p.y + nrm.y*off, p.z + nrm.z*off);

    float rhoS = trilinear(rhoA, nx,ny,nz, ps.x,ps.y,ps.z, mnX,mnY,mnZ, csX,csY,csZ);
    if (rhoS < 1e-4f) rhoS = 1.0f;

    // p - p0 = cs^2 * (rho - rho0), cs^2 = 1/3; q = 0.5*U^2
    float cp = (rhoS - 1.0f) / (1.5f * fmaxf(Ulat*Ulat, 1e-6f));
    if (cp >  1.0f) cp =  1.0f;
    if (cp < -3.0f) cp = -3.0f;

    float t = (cp + 1.0f) * 0.5f;
    if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
    float3 c;
    if      (t < 0.25f) { float k = t/0.25f;        c = make_float3(0.f, k, 1.f); }
    else if (t < 0.5f)  { float k = (t-0.25f)/0.25f; c = make_float3(0.f, 1.f, 1.f-k); }
    else if (t < 0.75f) { float k = (t-0.5f)/0.25f;  c = make_float3(k, 1.f, 0.f); }
    else                { float k = (t-0.75f)/0.25f; c = make_float3(1.f, 1.f-k, 0.f); }
    outCol[i] = c;
}

// =====================================================
// Host API
// =====================================================
static void computeUlat(const FlowParams& prm, float3& flowDirW, float3& flowDirL, float3& uIn) {
    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    if (vmag < 1e-5f) { flowDirW = make_float3(1,0,0); }
    else flowDirW = make_float3(prm.vx/vmag, prm.vy/vmag, prm.vz/vmag);
    flowDirL = flowDirW;
    g_Ulat = 0.02f + 0.013f*vmag;
    if (g_Ulat < 0.02f) g_Ulat = 0.02f;
    if (g_Ulat > 0.15f) g_Ulat = 0.15f;
    uIn = make_float3(flowDirL.x*g_Ulat, flowDirL.y*g_Ulat, flowDirL.z*g_Ulat);
}

bool lbmReady() { return s_ready; }

void initLBM() {
    releaseLBM();
    float* dist = getCudaDistField();
    if (dist == nullptr || g_voxNx <= 4 || g_voxNy <= 4 || g_voxNz <= 4) {
        std::cerr << "LBM: no voxel field/CUDA - solver not started" << std::endl;
        s_ready = false;
        return;
    }
    h_nx = g_voxNx; h_ny = g_voxNy; h_nz = g_voxNz;
    h_cells = h_nx*h_ny*h_nz;

    static bool constsUploaded = false;
    if (!constsUploaded) {
        cudaMemcpyToSymbol(c_dirs, H_DIRS, sizeof(H_DIRS));
        cudaMemcpyToSymbol(c_opp,  H_OPP,  sizeof(H_OPP));
        cudaMemcpyToSymbol(c_w,    H_W,    sizeof(H_W));
        constsUploaded = true;
    }

    if (cudaMalloc(&d_fA, (size_t)h_cells*Q*sizeof(float)) != cudaSuccess) { std::cerr<<"LBM: alloc fA fail\n"; return; }
    if (cudaMalloc(&d_fB, (size_t)h_cells*Q*sizeof(float)) != cudaSuccess) { std::cerr<<"LBM: alloc fB fail\n"; return; }
    if (cudaMalloc(&d_rho, h_cells*sizeof(float)) != cudaSuccess) { std::cerr<<"LBM: alloc rho fail\n"; return; }
    if (cudaMalloc(&d_ux, h_cells*sizeof(float)) != cudaSuccess) { std::cerr<<"LBM: alloc ux fail\n"; return; }
    if (cudaMalloc(&d_uy, h_cells*sizeof(float)) != cudaSuccess) { std::cerr<<"LBM: alloc uy fail\n"; return; }
    if (cudaMalloc(&d_uz, h_cells*sizeof(float)) != cudaSuccess) { std::cerr<<"LBM: alloc uz fail\n"; return; }
    if (cudaMalloc(&d_fSum, 3*sizeof(float)) != cudaSuccess) { std::cerr<<"LBM: alloc fSum fail\n"; return; }

    FlowParams prm = flowParams;
    float3 fdW, fdL, uIn;
    computeUlat(prm, fdW, fdL, uIn);

    int bs = 128, nb = (h_cells + bs - 1)/bs;
    lbmInitKernel<<<nb, bs>>>(d_fA, d_rho, d_ux, d_uy, d_uz, dist,
                              h_nx, h_ny, h_nz, uIn);
    cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) { std::cerr<<"LBM init kernel: "<<cudaGetErrorString(e)<<std::endl; return; }
    cudaMemcpy(d_fB, d_fA, (size_t)h_cells*Q*sizeof(float), cudaMemcpyDeviceToDevice);

    s_ready = true;
    std::cout << "LBM: " << h_nx << "x" << h_ny << "x" << h_nz
              << " (" << h_cells << " cells, " << (h_cells*Q*4/1024/1024) << " MB x2)" << std::endl;
}

void releaseLBM() {
    if (d_fA) { cudaFree(d_fA); d_fA = nullptr; }
    if (d_fB) { cudaFree(d_fB); d_fB = nullptr; }
    if (d_rho) { cudaFree(d_rho); d_rho = nullptr; }
    if (d_ux) { cudaFree(d_ux); d_ux = nullptr; }
    if (d_uy) { cudaFree(d_uy); d_uy = nullptr; }
    if (d_uz) { cudaFree(d_uz); d_uz = nullptr; }
    if (d_fSum) { cudaFree(d_fSum); d_fSum = nullptr; }
    if (d_ppos) { cudaFree(d_ppos); d_ppos = nullptr; }
    if (d_pcol) { cudaFree(d_pcol); d_pcol = nullptr; }
    d_pcap = 0;
    s_ready = false;
}

void stepLBM(int substeps, const FlowParams& prm, float nuLattice,
             float* outDragCoef, float* outLiftCoef)
{
    *outDragCoef = 0.f; *outLiftCoef = 0.f;
    if (!s_ready) return;

    float* dist = getCudaDistField();
    if (!dist) return;

    float3 fdW, fdL, uIn;
    computeUlat(prm, fdW, fdL, uIn);
    float tau0 = 3.0f*nuLattice + 0.5f;

    if (substeps < 1) substeps = 1;
    if (substeps > 200) substeps = 200;

    cudaMemset(d_fSum, 0, 3*sizeof(float));

    int bs = 128, nb = (h_cells + bs - 1)/bs;
    for (int s = 0; s < substeps; s++) {
        collideStreamKernel<<<nb, bs>>>(d_fA, d_fB, d_rho, d_ux, d_uy, d_uz, dist,
                                        h_nx, h_ny, h_nz, tau0, 0.12f, uIn, d_fSum);
        boundaryKernel<<<nb, bs>>>(d_fB, dist, h_nx, h_ny, h_nz, fdL, uIn);
        float* t = d_fA; d_fA = d_fB; d_fB = t;
    }
    cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) { std::cerr<<"LBM step: "<<cudaGetErrorString(e)<<std::endl; return; }

    float hsum[3];
    cudaMemcpy(hsum, d_fSum, 3*sizeof(float), cudaMemcpyDeviceToHost);
    float invSub = 1.0f/substeps;
    float Fx = hsum[0]*invSub, Fy = hsum[1]*invSub, Fz = hsum[2]*invSub;

    float drag = Fx*fdW.x + Fy*fdW.y + Fz*fdW.z;
    float upx = -fdW.x*fdW.y, upy = 1.0f - fdW.y*fdW.y, upz = -fdW.z*fdW.y;
    float upl = sqrtf(upx*upx + upy*upy + upz*upz);
    float clProj = 0.f;
    if (upl > 1e-4f) clProj = (Fy*upy + Fx*upx + Fz*upz)/upl;

    float sx = (g_voxMaxX-g_voxMinX), sy = (g_voxMaxY-g_voxMinY), sz = (g_voxMaxZ-g_voxMinZ);
    float csx = prm.cellSizeX, csy = prm.cellSizeY, csz = prm.cellSizeZ;
    float cm = (csx+csy+csz)/3.0f;
    float A_world = sy*sz*fabsf(fdW.x) + sx*sz*fabsf(fdW.y) + sx*sy*fabsf(fdW.z);
    float A_cells = A_world / (cm*cm);
    if (A_cells < 1.0f) A_cells = 1.0f;

    float U2 = g_Ulat*g_Ulat;
    float cd = 2.0f*drag / (U2*A_cells);
    float cl = 2.0f*clProj / (U2*A_cells);

    dragVector = glm::vec3(fdW.x, fdW.y, fdW.z) * cd * maxDim;
    if (upl > 1e-4f) {
        glm::vec3 liftDirW(upx/upl, upy/upl, upz/upl);
        liftVector = liftDirW * cl * maxDim;
    } else liftVector = glm::vec3(0.0f);

    *outDragCoef = cd;
    *outLiftCoef = cl;
}

static void ensureParticleBuffers(int n) {
    if (d_pcap >= n && d_ppos && d_pcol) return;
    if (d_ppos) { cudaFree(d_ppos); d_ppos = nullptr; }
    if (d_pcol) { cudaFree(d_pcol); d_pcol = nullptr; }
    cudaMalloc(&d_ppos, (size_t)n*sizeof(float3));
    cudaMalloc(&d_pcol, (size_t)n*sizeof(float3));
    d_pcap = n;
}

void updateParticlesLBM(std::vector<float>& pos, std::vector<float>& col,
                        int n, const FlowParams& prm, float dt)
{
    if (!s_ready || n <= 0) return;
    float* dist = getCudaDistField();
    if (!dist) return;

    float3 fdW, fdL, uIn;
    computeUlat(prm, fdW, fdL, uIn);
    float dtEff = dt * prm.timeScale;

    float extX = (g_voxMaxX-g_voxMinX), extY = (g_voxMaxY-g_voxMinY), extZ = (g_voxMaxZ-g_voxMinZ);
    float depthAlong = fabsf(fdW.x)*extX + fabsf(fdW.y)*extY + fabsf(fdW.z)*extZ;
    float3 spawnCenter = make_float3(
        (g_voxMinX+g_voxMaxX)*0.5f - fdW.x*0.45f*depthAlong,
        (g_voxMinY+g_voxMaxY)*0.5f - fdW.y*0.45f*depthAlong,
        (g_voxMinZ+g_voxMaxZ)*0.5f - fdW.z*0.45f*depthAlong);
    float3 upRef = make_float3(0,1,0);
    if (fabsf(fdW.y) > 0.9f) upRef = make_float3(1,0,0);
    float3 right = make_float3(fdW.y*upRef.z - fdW.z*upRef.y,
                               fdW.z*upRef.x - fdW.x*upRef.z,
                               fdW.x*upRef.y - fdW.y*upRef.x);
    float rl = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
    if (rl > 1e-6f) { right.x/=rl; right.y/=rl; right.z/=rl; }
    float3 up = make_float3(right.y*fdW.z - right.z*fdW.y,
                            right.z*fdW.x - right.x*fdW.z,
                            right.x*fdW.y - right.y*fdW.x);
    float spreadR = fabsf(right.x)*extX + fabsf(right.y)*extY + fabsf(right.z)*extZ;
    float spreadU = fabsf(up.x)*extX    + fabsf(up.y)*extY    + fabsf(up.z)*extZ;

    ensureParticleBuffers(n);
    cudaMemcpy(d_ppos, pos.data(), (size_t)n*3*sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_pcol, col.data(), (size_t)n*3*sizeof(float), cudaMemcpyHostToDevice);

    int bs = 128, nb = (n + bs - 1)/bs;
    advectParticlesKernel<<<nb, bs>>>((float3*)d_ppos, (float3*)d_pcol, n,
                                      dist, d_ux, d_uy, d_uz,
                                      h_nx, h_ny, h_nz,
                                      g_voxMinX, g_voxMinY, g_voxMinZ,
                                      prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ,
                                      fdW, sqrtf(prm.vx*prm.vx+prm.vy*prm.vy+prm.vz*prm.vz),
                                      g_Ulat, dtEff,
                                      make_float3(prm.minX, prm.minY, prm.minZ),
                                      make_float3(prm.maxX, prm.maxY, prm.maxZ),
                                      spawnCenter, right, up, spreadR, spreadU,
                                      prm.time, prm.maxSpeed, prm.cellSizeX);
    cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) { std::cerr<<"LBM advect: "<<cudaGetErrorString(e)<<std::endl; return; }

    cudaMemcpy(pos.data(), d_ppos, (size_t)n*3*sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(col.data(), d_pcol, (size_t)n*3*sizeof(float), cudaMemcpyDeviceToHost);
}

void surfacePressureLBM(const std::vector<float>& verts, std::vector<float>& outCol,
                        int n, const FlowParams& prm)
{
    if (!s_ready || n <= 0) return;
    float* dist = getCudaDistField();
    if (!dist) return;

    float3 *dv, *dc;
    if (cudaMalloc(&dv, (size_t)n*sizeof(float3)) != cudaSuccess) return;
    if (cudaMalloc(&dc, (size_t)n*sizeof(float3)) != cudaSuccess) { cudaFree(dv); return; }
    cudaMemcpy(dv, verts.data(), (size_t)n*3*sizeof(float), cudaMemcpyHostToDevice);

    int bs = 128, nb = (n + bs - 1)/bs;
    surfacePressureKernel<<<nb, bs>>>(dv, dc, n, dist, d_rho,
                                      h_nx, h_ny, h_nz,
                                      g_voxMinX, g_voxMinY, g_voxMinZ,
                                      prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ,
                                      g_Ulat);
    cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) { std::cerr<<"LBM pressure: "<<cudaGetErrorString(e)<<std::endl; }
    else {
        outCol.resize((size_t)n*3);
        cudaMemcpy(outCol.data(), dc, (size_t)n*3*sizeof(float), cudaMemcpyDeviceToHost);
    }
    cudaFree(dv);
    cudaFree(dc);
}