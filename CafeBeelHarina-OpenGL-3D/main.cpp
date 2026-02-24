// ===============================
// CafeBeelHarina - OpenGL 3D
// main.cpp (UPDATED - TEXTURE ASSIGNMENT IMPLEMENTED)
// - Simple Texture (no surface color)
// - Blended Texture + Surface Color
//   - Color computed on VERTEX
//   - Color computed on FRAGMENT
// - Textured curvy objects: Sphere + Cone
// - Keyboard toggles for all features
// ===============================

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Shader.h"
#include "Camera.h"
#include "Sphere.h"
#include "Cylinder.h"
#include "stb_image.h"

#include <iostream>
#include <vector>
#include <cmath>

// ------------------------------
// Function Prototypes
// ------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

// Texture utils
unsigned int loadTexture(char const* path,
    GLint wrapS = GL_REPEAT,
    GLint wrapT = GL_REPEAT,
    GLint minFilter = GL_LINEAR,
    GLint magFilter = GL_LINEAR);

unsigned int createSolidTextureRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

// ------------------------------
// Settings
// ------------------------------
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 900;

// ------------------------------
// Camera
// ------------------------------
Camera camera(glm::vec3(0.0f, 2.0f, 10.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// Camera Modes
enum CameraMode { FPS, STATIC, TOP, ORBIT };
CameraMode currentCameraMode = FPS;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Scene State
bool emissiveOn = true;   // kept (for future)
bool isWireframe = false;

// Texture Mapping State (wrap/filter keys already exist)
GLint wrapModes[] = { GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE };
int currentWrap = 0;
GLint filterModes[] = { GL_NEAREST, GL_LINEAR };
int currentFilter = 1;

// Textures
unsigned int woodTexture = 0, waterTexture = 0, canopyTexture = 0;

// ======================================================
// ASSIGNMENT TOGGLES
// ======================================================
enum TexFeatureMode {
    TEX_OFF = 0,          // no texture (baseColor only)
    TEX_SIMPLE = 1,       // simple texture (no surface color)
    TEX_BLEND_VERTEX = 2, // blended with baseColor, computed on VERTEX shader
    TEX_BLEND_FRAGMENT = 3// blended with baseColor, computed on FRAGMENT shader
};
TexFeatureMode gTexMode = TEX_SIMPLE;

// Apply current mode to shader uniforms
static void applyTexModeToShader(Shader& shader)
{
    if (gTexMode == TEX_OFF) {
        shader.setBool("uUseTexture", false);
        shader.setBool("uBlendWithColor", false);
        shader.setInt("uComputeMode", 1);
    }
    else if (gTexMode == TEX_SIMPLE) {
        shader.setBool("uUseTexture", true);
        shader.setBool("uBlendWithColor", false);
        shader.setInt("uComputeMode", 1); // fragment is fine
    }
    else if (gTexMode == TEX_BLEND_VERTEX) {
        shader.setBool("uUseTexture", true);
        shader.setBool("uBlendWithColor", true);
        shader.setInt("uComputeMode", 0); // vertex computed
    }
    else { // TEX_BLEND_FRAGMENT
        shader.setBool("uUseTexture", true);
        shader.setBool("uBlendWithColor", true);
        shader.setInt("uComputeMode", 1); // fragment computed
    }
}

static void bindTex0(Shader& shader, unsigned int texID, int unit = 0)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texID);
    shader.setInt("uTex0", unit);
}

// ======================================================
// Cube Vertices (pos, normal, uv)
// ======================================================
float cubeVertices[] = {
    // positions          // normals           // tex coords
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,

    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,

    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,

    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f
};

// ======================================================
// Simple Cone (curvy object)
// ======================================================
struct SimpleCone {
    unsigned int VAO = 0, VBO = 0;
    int vertexCount = 0;

    void build(int segments = 40) {
        const float PI = 3.1415926535f;
        std::vector<float> v; // pos(3) normal(3) uv(2)

        for (int i = 0; i < segments; i++) {
            float a0 = (float)i / segments * 2.0f * PI;
            float a1 = (float)(i + 1) / segments * 2.0f * PI;

            glm::vec3 p0(cos(a0), 0.0f, sin(a0));
            glm::vec3 p1(cos(a1), 0.0f, sin(a1));
            glm::vec3 tip(0.0f, 1.0f, 0.0f);

            auto push = [&](glm::vec3 p, glm::vec2 uv) {
                glm::vec3 n = glm::normalize(glm::vec3(p.x, 0.6f, p.z));
                v.insert(v.end(), { p.x,p.y,p.z, n.x,n.y,n.z, uv.x,uv.y });
            };

            push(p0, glm::vec2((float)i / segments, 0.0f));
            push(p1, glm::vec2((float)(i + 1) / segments, 0.0f));
            push(tip, glm::vec2(((float)i + 0.5f) / segments, 1.0f));
        }

        vertexCount = (int)(v.size() / 8);

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);
    }

    void draw() {
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        glBindVertexArray(0);
    }
};

SimpleCone gCone; // global cone

// ======================================================
// Draw Cube (TEXTURE ENABLED)
// ======================================================
void drawCube(Shader& shader, unsigned int vao,
    glm::vec3 pos, glm::vec3 scale,
    glm::vec4 color,
    unsigned int texID = 0)
{
    shader.setV4("baseColor", color);

    // For sky/water you will explicitly disable uUseTexture elsewhere,
    // but for general objects use the selected assignment mode.
    applyTexModeToShader(shader);

    if (texID != 0 && gTexMode != TEX_OFF) {
        bindTex0(shader, texID, 0);
    }
    else {
        shader.setBool("uUseTexture", false);
    }

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, pos);
    model = glm::scale(model, scale);
    shader.setMat4("model", model);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

// ======================================================
// Cafe Objects (Realistic Curvy Objects)
// ======================================================
void drawMug(Shader& shader, Cylinder& cylinder, glm::vec3 pos, float radius, float height, glm::vec4 color, unsigned int texID = 0) {
    // Body - height is along the cylinder's local Z axis (before rotation)
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, pos);
    model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0)); // Z-axis to Y-axis
    model = glm::scale(model, glm::vec3(radius, radius, height));
    shader.setMat4("model", model);
    shader.setV4("baseColor", color);
    
    applyTexModeToShader(shader);
    if (texID != 0 && gTexMode != TEX_OFF) bindTex0(shader, texID, 0);
    else shader.setBool("uUseTexture", false);
    
    cylinder.draw();

    // Handle (small vertical cylinder segment on the side)
    model = glm::mat4(1.0f);
    model = glm::translate(model, pos + glm::vec3(radius * 0.9f, 0, 0));
    model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(radius * 0.25f, radius * 0.25f, height * 0.7f));
    shader.setMat4("model", model);
    shader.setV4("baseColor", color * 0.8f);
    shader.setBool("uUseTexture", false); 
    cylinder.draw();
}

void drawCup(Shader& shader, Cylinder& cylinder, glm::vec3 pos, float radius, float height, glm::vec4 color, unsigned int texID = 0) {
    // Body
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, pos);
    model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(radius, radius, height));
    shader.setMat4("model", model);
    shader.setV4("baseColor", color);

    applyTexModeToShader(shader);
    if (texID != 0 && gTexMode != TEX_OFF) bindTex0(shader, texID, 0);
    else shader.setBool("uUseTexture", false);

    cylinder.draw();
}

// Outline helper
void drawCubeWithOutline(Shader& shader, unsigned int vao,
    glm::vec3 pos, glm::vec3 scale,
    glm::vec4 color, float outlineThickness = 0.05f)
{
    glm::vec4 outlineColor = glm::vec4(0.05f, 0.05f, 0.07f, 1.0f);
    drawCube(shader, vao, pos, scale + glm::vec3(outlineThickness), outlineColor, 0);
    drawCube(shader, vao, pos, scale, color, 0);
}

// ======================================================
// Stylized Table Set (textured)
// ======================================================
void drawStylizedTableSet(Shader& shader, Sphere& sphere, Cylinder& cylinder, unsigned int vao, glm::vec3 offset, float zDist)
{
    float depthScale = 1.0f - glm::clamp((zDist + 5.0f) / 100.0f, 0.0f, 0.15f);

    glm::vec4 tableColor = glm::vec4(0.70f, 0.48f, 0.25f, 1.0f);
    glm::vec4 chairColor = glm::vec4(0.20f, 0.14f, 0.10f, 1.0f);

    float rot = 5.0f * sin(offset.x * 0.5f + offset.z * 0.3f);
    float vH = 0.2f;

    auto drawPart = [&](glm::vec3 pos, glm::vec3 scale, glm::vec4 color, unsigned int texID) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, offset);
        model = glm::rotate(model, glm::radians(rot), glm::vec3(0, 1, 0));
        model = glm::translate(model, pos + glm::vec3(0, vH, 0));
        model = glm::scale(model, scale * depthScale);

        shader.setMat4("model", model);
        shader.setV4("baseColor", color);

        applyTexModeToShader(shader);
        if (texID != 0 && gTexMode != TEX_OFF) bindTex0(shader, texID, 0);
        else shader.setBool("uUseTexture", false);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    };

    // Table (wood texture)
    drawPart(glm::vec3(0, 0.62f, 0), glm::vec3(2.4f, 0.12f, 1.4f), tableColor, woodTexture);
    drawPart(glm::vec3(0, 0.58f, 0), glm::vec3(2.42f, 0.08f, 1.42f), tableColor * 0.88f, woodTexture);

    // Mugs/Cups on table (Table top surface is at Y=0.88)
    // Mug (Sit on surface: item height 0.5 -> center at 0.88 + 0.25 = 1.13)
    drawMug(shader, cylinder, offset + glm::vec3(-0.6f, 1.13f, 0.3f), 0.18f * depthScale, 0.5f * depthScale, glm::vec4(1.0f, 0.95f, 0.9f, 1.0f), waterTexture);
    // Cup (Sit on surface: item height 0.4 -> center at 0.88 + 0.20 = 1.08)
    drawCup(shader, cylinder, offset + glm::vec3(0.6f, 1.08f, -0.3f), 0.20f * depthScale, 0.4f * depthScale, glm::vec4(0.5f, 0.8f, 1.0f, 1.0f), waterTexture);

    // Small "Curvy" Objects (Sphere + Cone as buns/vases)
    applyTexModeToShader(shader);
    if (gTexMode != TEX_OFF) bindTex0(shader, waterTexture, 0); 
    else shader.setBool("uUseTexture", false);

    // Small Sphere (Bun/Fruit - radius 0.15 -> center at 0.88 + 0.15 = 1.03)
    glm::mat4 sM = glm::mat4(1.0f);
    sM = glm::translate(sM, offset + glm::vec3(-0.2f, 1.03f, -0.2f));
    sM = glm::scale(sM, glm::vec3(0.15f * depthScale));
    shader.setMat4("model", sM);
    shader.setV4("baseColor", glm::vec4(0.9f, 0.7f, 0.3f, 1.0f));
    sphere.draw();

    // Small Tapered Object (Cone as a small vase - height 0.4 -> center at 0.88 + 0.2 = 1.08)
    sM = glm::mat4(1.0f);
    sM = glm::translate(sM, offset + glm::vec3(0.2f, 1.08f, 0.5f));
    sM = glm::scale(sM, glm::vec3(0.12f, 0.4f, 0.12f) * depthScale);
    shader.setMat4("model", sM);
    shader.setV4("baseColor", glm::vec4(0.8f, 0.4f, 0.2f, 1.0f));
    gCone.draw();

    // Legs
    drawPart(glm::vec3(-0.9f, 0.3f, -0.5f), glm::vec3(0.12f, 0.8f, 0.12f), tableColor * 0.9f, woodTexture);
    drawPart(glm::vec3(0.9f, 0.3f, -0.5f), glm::vec3(0.12f, 0.8f, 0.12f), tableColor * 0.9f, woodTexture);
    drawPart(glm::vec3(-0.9f, 0.3f, 0.5f), glm::vec3(0.12f, 0.8f, 0.12f), tableColor * 0.9f, woodTexture);
    drawPart(glm::vec3(0.9f, 0.3f, 0.5f), glm::vec3(0.12f, 0.8f, 0.12f), tableColor * 0.9f, woodTexture);

    // Chairs (wood texture too)
    glm::vec3 chairPos[] = {
        glm::vec3(0.0f, 0.0f, 1.6f),
        glm::vec3(0.0f, 0.0f, -1.6f),
        glm::vec3(1.7f, 0.0f, 0.0f),
        glm::vec3(-1.7f, 0.0f, 0.0f)
    };
    float chairRots[] = { 0.0f, 180.0f, 90.0f, -90.0f };

    for (int i = 0; i < 4; ++i) {
        float chairRot = chairRots[i];

        auto drawChairPart = [&](glm::vec3 p, glm::vec3 s, glm::vec4 c, unsigned int texID) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, offset);
            model = glm::rotate(model, glm::radians(rot), glm::vec3(0, 1, 0));
            model = glm::translate(model, chairPos[i] + glm::vec3(0, vH, 0));
            model = glm::rotate(model, glm::radians(chairRot), glm::vec3(0, 1, 0));
            model = glm::translate(model, p);
            model = glm::scale(model, s * depthScale);

            shader.setMat4("model", model);
            shader.setV4("baseColor", c);

            applyTexModeToShader(shader);
            if (texID != 0 && gTexMode != TEX_OFF) bindTex0(shader, texID, 0);
            else shader.setBool("uUseTexture", false);

            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        };

        drawChairPart(glm::vec3(0, 0.42f, 0), glm::vec3(1.1f, 0.15f, 1.1f), chairColor, woodTexture);
        drawChairPart(glm::vec3(0, 1.0f, 0.5f), glm::vec3(1.1f, 1.0f, 0.1f), chairColor, woodTexture);

        drawChairPart(glm::vec3(-0.4f, 0.2f, -0.4f), glm::vec3(0.15f, 0.4f, 0.15f), chairColor * 0.85f, woodTexture);
        drawChairPart(glm::vec3(0.4f, 0.2f, -0.4f), glm::vec3(0.15f, 0.4f, 0.15f), chairColor * 0.85f, woodTexture);
        drawChairPart(glm::vec3(-0.4f, 0.2f, 0.4f), glm::vec3(0.15f, 0.4f, 0.15f), chairColor * 0.85f, woodTexture);
        drawChairPart(glm::vec3(0.4f, 0.2f, 0.4f), glm::vec3(0.15f, 0.4f, 0.15f), chairColor * 0.85f, woodTexture);
    }
}

// ======================================================
// Full Scene
// ======================================================
void drawRiversideScene(Shader& shader, Sphere& sphere, Cylinder& cylinder, unsigned int cubeVAO, float time)
{
    glm::mat4 model;

    shader.setBool("isDeck", false);
    shader.setBool("isSky", false);
    shader.setBool("isWater", false);

    // ---------- SKY ----------
    shader.setBool("isSky", true);
    shader.setBool("uUseTexture", false);    // IMPORTANT: don't texture sky
    shader.setInt("uComputeMode", 1);
    shader.setV4("skyTop", glm::vec4(0.62f, 0.82f, 0.97f, 1.0f));
    shader.setV4("skyBottom", glm::vec4(0.52f, 0.76f, 0.95f, 1.0f));
    drawCube(shader, cubeVAO, glm::vec3(0, 30, -85), glm::vec3(400, 300, 1), glm::vec4(1.0f), 0);
    shader.setBool("isSky", false);

    // ---------- WATER ----------
    shader.setBool("isWater", true);
    shader.setBool("uUseTexture", false);    // IMPORTANT: don't texture water in this look
    shader.setInt("uComputeMode", 1);
    shader.setFloat("time", time);
    shader.setV4("waterDeep", glm::vec4(0.03f, 0.14f, 0.34f, 1.0f));
    shader.setV4("waterHorizon", glm::vec4(0.18f, 0.40f, 0.72f, 1.0f));

    model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(0.0f, -2.5f, 0.0f));
    model = glm::scale(model, glm::vec3(260.0f, 0.1f, 260.0f));
    shader.setMat4("model", model);
    shader.setV4("baseColor", glm::vec4(0.03f, 0.14f, 0.34f, 1.0f));
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    shader.setBool("isWater", false);

    // ---------- FLOOR / WOOD ----------
    glm::vec4 floorTop = glm::vec4(0.82f, 0.68f, 0.45f, 1.0f);
    glm::vec4 floorSide = glm::vec4(0.60f, 0.48f, 0.30f, 1.0f);
    glm::vec4 deckWood = glm::vec4(0.68f, 0.45f, 0.22f, 1.0f);

    float floorH = 0.4f;
    float floorY = 0.3f;

    // Entrance wooden walkway (textured)
    drawCube(shader, cubeVAO, glm::vec3(0, floorY, 12.5f), glm::vec3(3.0f, floorH, 19.0f), deckWood, woodTexture);
    drawCube(shader, cubeVAO, glm::vec3(0, floorY - 0.3f, 12.5f), glm::vec3(3.1f, 0.2f, 19.0f), deckWood * 0.6f, woodTexture);

    // Main dining floor (textured)
    drawCube(shader, cubeVAO, glm::vec3(0, floorY, -5.0f), glm::vec3(39.0f, floorH, 16.0f), floorTop, woodTexture);
    drawCube(shader, cubeVAO, glm::vec3(0, floorY - 0.3f, -5.0f), glm::vec3(39.1f, 0.2f, 16.0f), floorSide, woodTexture);

    // Back floor
    drawCube(shader, cubeVAO, glm::vec3(0, floorY, -20.0f), glm::vec3(18.0f, floorH, 14.0f), floorTop, woodTexture);

    // ---------- Canopy frames ----------
    glm::vec3 frameCenters[] = {
        glm::vec3(-10.5f, floorY, -5.0f),
        glm::vec3(0.0f, floorY, -20.0f),
        glm::vec3(10.5f, floorY, -5.0f)
    };

    glm::vec4 frameColor = glm::vec4(0.18f, 0.19f, 0.22f, 1.0f);
    glm::vec4 glassColor = glm::vec4(0.72f, 0.86f, 1.0f, 0.18f);

    for (int p = 0; p < 3; ++p) {
        glm::vec3 offset = frameCenters[p];
        float fW = (p == 1) ? 6.5f : 9.0f;
        float fD = (p == 1) ? 5.5f : 6.8f;

        glm::vec3 corners[] = {
            offset + glm::vec3(-fW, 2.5f, -fD), offset + glm::vec3(fW, 2.5f, -fD),
            offset + glm::vec3(-fW, 2.5f, fD),  offset + glm::vec3(fW, 2.5f, fD)
        };

        for (int i = 0; i < 4; ++i)
            drawCube(shader, cubeVAO, corners[i], glm::vec3(0.2f, 5.0f, 0.2f), frameColor, 0);

        float bT = 0.15f;
        drawCube(shader, cubeVAO, offset + glm::vec3(0, 4.9f, -fD), glm::vec3(fW * 2.1f, bT, bT), frameColor, 0);
        drawCube(shader, cubeVAO, offset + glm::vec3(0, 4.9f,  fD), glm::vec3(fW * 2.1f, bT, bT), frameColor, 0);
        drawCube(shader, cubeVAO, offset + glm::vec3(-fW, 4.9f, 0), glm::vec3(bT, bT, fD * 2.1f), frameColor, 0);
        drawCube(shader, cubeVAO, offset + glm::vec3( fW, 4.9f, 0), glm::vec3(bT, bT, fD * 2.1f), frameColor, 0);

        // Bulbs (no texture)
        for (int i = 0; i < 4; ++i) {
            float x = -fW + (i * (fW * 2.0f) / 3.0f);

            shader.setBool("uUseTexture", false);
            shader.setInt("uComputeMode", 1);

            glm::mat4 modelBulb = glm::mat4(1.0f);
            modelBulb = glm::translate(modelBulb, offset + glm::vec3(x, 4.82f, -fD + 0.1f));
            modelBulb = glm::scale(modelBulb, glm::vec3(0.25f));
            shader.setMat4("model", modelBulb);
            shader.setV4("baseColor", glm::vec4(1.0f, 0.88f, 0.55f, 1.0f));
            sphere.draw();

            modelBulb = glm::mat4(1.0f);
            modelBulb = glm::translate(modelBulb, offset + glm::vec3(x, 4.82f, fD - 0.1f));
            modelBulb = glm::scale(modelBulb, glm::vec3(0.25f));
            shader.setMat4("model", modelBulb);
            shader.setV4("baseColor", glm::vec4(0.98f, 0.95f, 0.55f, 1.0f));
            sphere.draw();
        }

        // Furniture (textured wood)
        if (p == 1) {
            drawStylizedTableSet(shader, sphere, cylinder, cubeVAO, offset + glm::vec3(-3.2f, 0, 0), offset.z);
            drawStylizedTableSet(shader, sphere, cylinder, cubeVAO, offset + glm::vec3( 3.2f, 0, 0), offset.z);
        }
        else {
            drawStylizedTableSet(shader, sphere, cylinder, cubeVAO, offset + glm::vec3(-3.8f, 0, -3.2f), offset.z);
            drawStylizedTableSet(shader, sphere, cylinder, cubeVAO, offset + glm::vec3( 3.8f, 0, -3.2f), offset.z);
            drawStylizedTableSet(shader, sphere, cylinder, cubeVAO, offset + glm::vec3(-3.8f, 0,  3.2f), offset.z);
            drawStylizedTableSet(shader, sphere, cylinder, cubeVAO, offset + glm::vec3( 3.8f, 0,  3.2f), offset.z);
        }
    }

    // ---------- Glass Walls (use canopyTexture) ----------
    for (int p = 0; p < 3; ++p) {
        glm::vec3 offset = frameCenters[p];
        float fW = (p == 1) ? 6.5f : 9.0f;
        float fD = (p == 1) ? 5.5f : 6.8f;

        if (p == 1) {
            drawCube(shader, cubeVAO, offset + glm::vec3(0.0f, 2.5f, -fD), glm::vec3(fW * 2.0f, 4.8f, 0.04f), glassColor, canopyTexture);
            drawCube(shader, cubeVAO, offset + glm::vec3(-fW, 2.5f, 0.0f), glm::vec3(0.04f, 4.8f, fD * 2.0f), glassColor, canopyTexture);
            drawCube(shader, cubeVAO, offset + glm::vec3( fW, 2.5f, 0.0f), glm::vec3(0.04f, 4.8f, fD * 2.0f), glassColor, canopyTexture);
        }
        else {
            float xEdge = (p == 0) ? -fW : fW;
            drawCube(shader, cubeVAO, offset + glm::vec3(xEdge, 2.5f, 0.0f), glm::vec3(0.04f, 4.8f, fD * 2.0f), glassColor, canopyTexture);
            drawCube(shader, cubeVAO, offset + glm::vec3(0.0f, 2.5f, -fD), glm::vec3(fW * 2.0f, 4.8f, 0.04f), glassColor, canopyTexture);
            drawCube(shader, cubeVAO, offset + glm::vec3(0.0f, 2.5f,  fD), glm::vec3(fW * 2.0f, 4.8f, 0.04f), glassColor, canopyTexture);
        }
    }

    // Railings
    glm::vec4 railGlass = glm::vec4(0.70f, 0.85f, 1.0f, 0.45f);
    drawCube(shader, cubeVAO, glm::vec3(-1.55f, 1.2f, 12.5f), glm::vec3(0.02f, 1.0f, 19.0f), railGlass, canopyTexture);
    drawCube(shader, cubeVAO, glm::vec3( 1.55f, 1.2f, 12.5f), glm::vec3(0.02f, 1.0f, 19.0f), railGlass, canopyTexture);

    glm::vec4 capColor = glm::vec4(0.92f, 0.93f, 0.91f, 1.0f);
    drawCube(shader, cubeVAO, glm::vec3(-1.55f, 1.7f, 12.5f), glm::vec3(0.06f, 0.06f, 19.0f), capColor, 0);
    drawCube(shader, cubeVAO, glm::vec3( 1.55f, 1.7f, 12.5f), glm::vec3(0.06f, 0.06f, 19.0f), capColor, 0);

    for (int i = 0; i < 6; ++i) {
        float z = 3.0f + i * 3.84f;
        drawCube(shader, cubeVAO, glm::vec3(-1.55f, 1.0f, z), glm::vec3(0.04f, 0.6f, 0.04f), capColor, 0);
        drawCube(shader, cubeVAO, glm::vec3( 1.55f, 1.0f, z), glm::vec3(0.04f, 0.6f, 0.04f), capColor, 0);
    }

}

// ======================================================
// MAIN
// ======================================================
int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Cafe Beel Harina - 3D Riverside", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed gladLoad\n";
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Shader ourShader("vertex_shader.vs", "fragment_shader.fs");

    // Cube VAO/VBO
    unsigned int VBO, cubeVAO;
    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    Sphere sphere(1.0f, 32, 16);
    Cylinder planter(1.0f, 1.0f, 1.0f, 16, 1);

    // Build cone
    gCone.build(40);

    // TEXTURES:
    // User conceptual mapping:
    // 1. Flat Surfaces (floor, tables): container2.png (Tileable for wrapping)
    // 2. Structures/Glass (railings, walls): container2_specular.png (Bordered for blending)
    // 3. Curvy Objects (mug, bun, sphere, cone): emoji.png (Colorful for mapping detail)

    const char* tilePath   = "D:\\4-2\\Lab\\CSE 4208 Computer Graphics Laboratory\\Lab_4\\container2.png";
    const char* borderPath = "D:\\4-2\\Lab\\CSE 4208 Computer Graphics Laboratory\\Lab_4\\container2_specular.png";
    const char* emojiPath  = "D:\\4-2\\Lab\\CSE 4208 Computer Graphics Laboratory\\Lab_4\\emoji.png";

    woodTexture   = loadTexture(tilePath,   wrapModes[currentWrap], wrapModes[currentWrap], filterModes[currentFilter], filterModes[currentFilter]);
    canopyTexture = loadTexture(borderPath, wrapModes[currentWrap], wrapModes[currentWrap], filterModes[currentFilter], filterModes[currentFilter]);
    waterTexture  = loadTexture(emojiPath,  wrapModes[currentWrap], wrapModes[currentWrap], filterModes[currentFilter], filterModes[currentFilter]);

    // Note: woodTexture is used for floor/tables/chairs.
    //       canopyTexture is used for glass/railings.
    //       waterTexture (emoji) is used for water and table items (mugs, buns, sphere, cone).

    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        glClearColor(0.55f, 0.75f, 0.95f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ourShader.use();
        glPolygonMode(GL_FRONT_AND_BACK, isWireframe ? GL_LINE : GL_FILL);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (height == 0) height = 1;

        float aspect = (float)width / (float)height;
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), aspect, 0.1f, 100.0f);

        glm::mat4 view;
        if (currentCameraMode == STATIC) {
            view = glm::lookAt(glm::vec3(0, 10, 15), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        }
        else if (currentCameraMode == TOP) {
            view = glm::lookAt(glm::vec3(0, 20, 0.1f), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        }
        else if (currentCameraMode == ORBIT) {
            float radius = 15.0f;
            float camX = sin((float)glfwGetTime()) * radius;
            float camZ = cos((float)glfwGetTime()) * radius;
            view = glm::lookAt(glm::vec3(camX, 5.0f, camZ), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        }
        else {
            view = camera.GetViewMatrix();
        }

        ourShader.setMat4("projection", projection);
        ourShader.setMat4("view", view);

        drawRiversideScene(ourShader, sphere, planter, cubeVAO, (float)glfwGetTime());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &VBO);
    glfwTerminate();
    return 0;
}

// ======================================================
// INPUT + CALLBACKS
// ======================================================
void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);

    static bool keys[1024] = { false };

    auto toggle = [&](int key, bool& val) {
        if (glfwGetKey(window, key) == GLFW_PRESS && !keys[key]) { val = !val; keys[key] = true; }
        else if (glfwGetKey(window, key) == GLFW_RELEASE) keys[key] = false;
    };

    toggle(GLFW_KEY_4, emissiveOn);
    toggle(GLFW_KEY_P, isWireframe);

    // ---------------------------
    // ASSIGNMENT: Texture toggles
    // 0: texture OFF (baseColor only)
    // 1: SIMPLE texture (no surface color)
    // 2: BLEND + VERTEX computed
    // 3: BLEND + FRAGMENT computed
    // ---------------------------
    if (glfwGetKey(window, GLFW_KEY_0) == GLFW_PRESS && !keys[GLFW_KEY_0]) {
        gTexMode = TEX_OFF;
        keys[GLFW_KEY_0] = true;
        std::cout << "TEX_OFF\n";
    }
    else if (glfwGetKey(window, GLFW_KEY_0) == GLFW_RELEASE) keys[GLFW_KEY_0] = false;

    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS && !keys[GLFW_KEY_1]) {
        gTexMode = TEX_SIMPLE;
        keys[GLFW_KEY_1] = true;
        std::cout << "TEX_SIMPLE (no surface color)\n";
    }
    else if (glfwGetKey(window, GLFW_KEY_1) == GLFW_RELEASE) keys[GLFW_KEY_1] = false;

    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS && !keys[GLFW_KEY_2]) {
        gTexMode = TEX_BLEND_VERTEX;
        keys[GLFW_KEY_2] = true;
        std::cout << "TEX_BLEND_VERTEX (computed on vertex)\n";
    }
    else if (glfwGetKey(window, GLFW_KEY_2) == GLFW_RELEASE) keys[GLFW_KEY_2] = false;

    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS && !keys[GLFW_KEY_3]) {
        gTexMode = TEX_BLEND_FRAGMENT;
        keys[GLFW_KEY_3] = true;
        std::cout << "TEX_BLEND_FRAGMENT (computed on fragment)\n";
    }
    else if (glfwGetKey(window, GLFW_KEY_3) == GLFW_RELEASE) keys[GLFW_KEY_3] = false;

    // Fullscreen Toggle (F)
    static bool isFullscreen = false;
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !keys[GLFW_KEY_F]) {
        isFullscreen = !isFullscreen;
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (isFullscreen) glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        else glfwSetWindowMonitor(window, NULL, 100, 100, SCR_WIDTH, SCR_HEIGHT, 0);
        keys[GLFW_KEY_F] = true;
    }
    else if (glfwGetKey(window, GLFW_KEY_F) == GLFW_RELEASE) keys[GLFW_KEY_F] = false;

    // cycle camera (C)
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && !keys[GLFW_KEY_C]) {
        currentCameraMode = (CameraMode)((currentCameraMode + 1) % 4);
        keys[GLFW_KEY_C] = true;
    }
    else if (glfwGetKey(window, GLFW_KEY_C) == GLFW_RELEASE) keys[GLFW_KEY_C] = false;

    // wrapping (R)
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !keys[GLFW_KEY_R]) {
        currentWrap = (currentWrap + 1) % 3;
        unsigned int texs[] = { woodTexture, waterTexture, canopyTexture };
        for (auto t : texs) {
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapModes[currentWrap]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapModes[currentWrap]);
        }
        keys[GLFW_KEY_R] = true;
        std::cout << "Wrap mode changed\n";
    }
    else if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) keys[GLFW_KEY_R] = false;

    // filtering (M)
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS && !keys[GLFW_KEY_M]) {
        currentFilter = (currentFilter + 1) % 2;
        unsigned int texs[] = { woodTexture, waterTexture, canopyTexture };
        for (auto t : texs) {
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterModes[currentFilter]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterModes[currentFilter]);
        }
        keys[GLFW_KEY_M] = true;
        std::cout << "Filter mode changed\n";
    }
    else if (glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE) keys[GLFW_KEY_M] = false;
}

void framebuffer_size_callback(GLFWwindow*, int width, int height)
{
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow*, double xposIn, double yposIn)
{
    float xpos = (float)xposIn;
    float ypos = (float)yposIn;

    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }

    camera.ProcessMouseMovement(xpos - lastX, lastY - ypos);
    lastX = xpos;
    lastY = ypos;
}

void scroll_callback(GLFWwindow*, double, double yoffset)
{
    camera.Zoom -= (float)yoffset;
    if (camera.Zoom < 1.0f) camera.Zoom = 1.0f;
    if (camera.Zoom > 45.0f) camera.Zoom = 45.0f;
}

// ======================================================
// TEXTURE LOADER
// ======================================================
unsigned int loadTexture(char const* path, GLint wrapS, GLint wrapT, GLint minFilter, GLint magFilter)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int w, h, n;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &w, &h, &n, 0);

    glBindTexture(GL_TEXTURE_2D, textureID);

    if (data)
    {
        GLenum format = (n == 1) ? GL_RED : (n == 3 ? GL_RGB : GL_RGBA);

        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Texture failed to load at path: " << path << "\n";

        unsigned char white[] = { 255, 255, 255, 255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    }

    return textureID;
}

// ======================================================
// ALWAYS-VALID SOLID TEXTURE
// ======================================================
unsigned int createSolidTextureRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    unsigned char px[4] = { r, g, b, a };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapModes[currentWrap]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapModes[currentWrap]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterModes[currentFilter]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterModes[currentFilter]);

    return tex;
}