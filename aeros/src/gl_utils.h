#ifndef GL_UTILS_H
#define GL_UTILS_H
// =====================================================
// GL-утилиты: компиляция шейдеров и служебная геометрия
// =====================================================

unsigned int compileProgram(const char* vsSrc, const char* fsSrc);
void createBoundingBoxVAO();
void createAxesVAO(float size);
void createObstacleSphere(int stacks = 48, int slices = 48);

#endif // GL_UTILS_H
