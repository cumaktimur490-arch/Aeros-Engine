#ifndef GL_UTILS_H
#define GL_UTILS_H

unsigned int compileProgram(const char* vsSrc, const char* fsSrc);
void createBoundingBoxVAO();
void createAxesVAO(float size);
void createObstacleSphere(int stacks, int slices);
void createGroundPlane(float size);
void createGrid(float size, int divisions);
void createSlicePlane(int axis, float pos01);
void cleanupGLResources();

#endif // GL_UTILS_H
