#ifndef INPUT_H
#define INPUT_H
// =====================================================
// Ввод: камера и управление
// =====================================================

struct GLFWwindow;

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

#endif // INPUT_H
