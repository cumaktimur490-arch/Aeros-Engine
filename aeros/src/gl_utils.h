#ifndef GL_UTILS_H
#define GL_UTILS_H

unsigned int compileProgram(const char* vsSrc, const char* fsSrc);
void createBoundingBoxVAO();
void createAxesVAO(float size);
void createObstacleSphere(int stacks, int slices);
void createGroundPlane(float size);
void createGrid(float size, int divisions);
void createSlicePlane(int axis, float pos01);
void renderCFDSliceGL(unsigned int modelShaderProgram,
                      const float* modelMat4, const float* viewMat4, const float* projMat4,
                      float alpha);
void renderInterestingGL(unsigned int particleShaderProgram,
                         unsigned int lineShaderProgram,
                         const float* modelMat4, const float* viewMat4, const float* projMat4);
void cleanupGLResources();

#endif // GL_UTILS_H
