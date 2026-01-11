#include "GlfwGeneral.hpp"


int main() {
    if (!InitializeWindow({ 1280, 720 }))
        return -1;
    while (!glfwWindowShouldClose(pWindow)) {

        /*äÖÈ¾¹ı³Ì£¬´ıÌî³ä*/

        glfwPollEvents();
        TitleFps();
    }
    TerminateWindow();
    return 0;
}