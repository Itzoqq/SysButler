#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <stdio.h>

int main(int, char**)
{
    // 1. Setup Window (Boilerplate)
    if (!glfwInit()) return 1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "System Butler", nullptr, nullptr);
    if (window == nullptr) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // 2. Setup ImGui (Boilerplate)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark(); // Enable "Hacker" Dark Mode
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // State for your toggle button
    bool show_hello = false;

    // 3. Main Loop (This runs 60 times a second)
    while (!glfwWindowShouldClose(window))
    {
        // Poll events (clicks, keys)
        glfwPollEvents();

        // Start a new UI frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- YOUR CODE STARTS HERE ---
        
        // Create a window inside the app
        ImGui::Begin("Dashboard"); 

        ImGui::Text("Welcome, User.");
        
        // The Logic you asked for:
        if (ImGui::Button("Toggle Message")) {
            show_hello = !show_hello; // Flip the boolean
        }

        if (show_hello) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Hello World!"); // Green text
        }

        ImGui::End();
        // --- YOUR CODE ENDS HERE ---

        // Render everything (Boilerplate)
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f); // Background color
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}