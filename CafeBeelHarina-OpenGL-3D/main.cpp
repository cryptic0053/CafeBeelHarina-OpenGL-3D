#define _CRT_SECURE_NO_WARNINGS
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
#include <ctime>

// Function Prototypes
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


// Settings
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 900;

// Phase 5 Player State
bool isWalkMode = true;
bool isSeated = false;
glm::vec3 preSeatPos = glm::vec3(0.0f);
bool hasCoffee = false;

float horizontalDistanceXZ(glm::vec3 p1, glm::vec3 p2) {
    return glm::length(glm::vec2(p1.x - p2.x, p1.z - p2.z));
}

// Camera
Camera camera(glm::vec3(0.0f, 1.5f, 24.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// Camera Modes
enum CameraMode { FPS, STATIC, TOP, ORBIT, BIRD_EYE };
CameraMode currentCameraMode = FPS;

// Look-at Orbit parameters
bool isLookAtOrbiting = false;
float orbitStartTime = 0.0f;
glm::vec3 orbitFocalPoint(0.0f);
glm::vec3 orbitStartPos(0.0f);

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Dynamic Animation States
bool isFanOn = false;
float fanAngle = 0.0f;
bool isDoorOpen = false;
float doorAngle = 0.0f;
bool isBrewing = false;
float brewAngle = 0.0f;

// Phase 6: AABB Collision Helper
bool isPositionValid(glm::vec3 pos) {
    if (!isWalkMode) return true; // Disabled during Free Fly

    float x = pos.x;
    float z = pos.z;

    // Walkway Limits (Narrowed to 1.45f to block glass rails fluidly)
    bool onWalkway = (x >= -1.45f && x <= 1.45f && z >= 1.5f && z <= 25.0f);
    // Main Dining Floor Limits
    bool onMainFloor = (x >= -20.0f && x <= 20.0f && z >= -13.0f && z <= 2.0f);
    // Back Dining Room Limits
    bool onBackFloor = (x >= -7.0f && x <= 7.0f && z >= -26.0f && z <= -13.0f);

    if (!onWalkway && !onMainFloor && !onBackFloor) return false;

    // Door Bound Extents
    if (!isDoorOpen) {
        if (x >= -1.6f && x <= 1.6f && z >= 1.6f && z <= 2.0f) return false; // Relaxed Z-depth for tighter opening distance
    }

    // Phase 7: Interior Collision 
    // Front Glass Wall Divider Blocks (stops sprinting past the closed door bounds)
    if (z > 1.6f && z < 2.0f) {
        if (x < -1.5f || x > 1.5f) return false; 
    }

    // Phase 7: Table & Machine Obstructions
    if (horizontalDistanceXZ(pos, glm::vec3(0.0f, 0.0f, -19.0f)) < 1.0f) return false; // Coffee Machine base

    glm::vec3 tables[] = {
        glm::vec3(-3.2f, 0, -20.0f), glm::vec3(3.2f, 0, -20.0f), // Back Canopy
        glm::vec3(-14.3f, 0, -8.2f), glm::vec3(-14.3f, 0, -1.8f), glm::vec3(-6.7f, 0, -8.2f), glm::vec3(-6.7f, 0, -1.8f), // Left Canopy
        glm::vec3(6.7f, 0, -8.2f), glm::vec3(6.7f, 0, -1.8f), glm::vec3(14.3f, 0, -8.2f), glm::vec3(14.3f, 0, -1.8f)      // Right Canopy
    };

    for (int i = 0; i < 10; ++i) {
        // Precise AABB boundary matching the exact dimensions of 1 table + 4 chairs.
        // Relaxed marginally to 1.9f (X) and 1.8f (Z) to allow extremely fluid movement between chairs without feeling rigid
        if (std::abs(x - tables[i].x) < 1.9f && std::abs(z - tables[i].z) < 1.8f) return false;
    }

    // Inner Glass Walls boundaries (explicit blocking inside canopies)
    if (std::abs(z - (-11.8f)) < 0.2f) {
        if (x < -1.5f || x > 1.5f) return false; // Main canopy back dividers
    }
    if (z > -26.0f && z < -14.0f) {
        if (std::abs(x - (-6.5f)) < 0.2f) return false; // Side glass back dining
        if (std::abs(x - 6.5f) < 0.2f) return false;    // Side glass back dining
    }

    return true;
}

// Scene State
bool emissiveOn = false;   
bool isWireframe = false;
bool isFourWayView = false;

// 7. Lighting Struct State Globals
bool masterLightOn = true;
bool dirLightOn    = true;
bool pointLightOn  = true;
bool spotLightOn   = true;
bool ambientOn     = true;

// Phase 8: NPCs and Seating
struct Seat {
    glm::vec3 pos;
    float facingAngle;
    int occupiedBy; // 0=Empty, 1=Player, 2+=NPC
};
std::vector<Seat> allSeats;
int playerSeatIdx = -1;

enum NPCState { ENTERING, WALKING_TO_JUNCTION, WALKING_TO_SEAT, SITTING, WALKING_OUT_JUNCTION, WALKING_OUT };
struct Customer {
    glm::vec3 pos;
    glm::vec3 target;
    NPCState state;
    int assignedSeat;
    float timer;
    float speed;
    bool active;
};
#define MAX_CUSTOMERS 3
Customer customers[MAX_CUSTOMERS];

void initSeats() {
    glm::vec3 hintTables[] = {
        glm::vec3(-3.2f, 0, -20.0f), glm::vec3(3.2f, 0, -20.0f), 
        glm::vec3(-14.3f, 0, -8.2f), glm::vec3(-14.3f, 0, -1.8f), glm::vec3(-6.7f, 0, -8.2f), glm::vec3(-6.7f, 0, -1.8f), 
        glm::vec3(6.7f, 0, -8.2f), glm::vec3(6.7f, 0, -1.8f), glm::vec3(14.3f, 0, -8.2f), glm::vec3(14.3f, 0, -1.8f)      
    };
    for(int i = 0; i < 10; ++i) {
        glm::vec3 center = hintTables[i];
        float rot = 5.0f * sin(center.x * 0.5f + center.z * 0.3f);
        glm::vec3 chairPos[] = {
            glm::vec3(0.0f, 0.0f, 1.6f), glm::vec3(0.0f, 0.0f, -1.6f),
            glm::vec3(1.7f, 0.0f, 0.0f), glm::vec3(-1.7f, 0.0f, 0.0f)
        };
        for(int c = 0; c < 4; ++c) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, center);
            model = glm::rotate(model, glm::radians(rot), glm::vec3(0, 1, 0));
            glm::vec4 cPos = model * glm::vec4(chairPos[c], 1.0f);
            Seat s;
            s.pos = glm::vec3(cPos);
            s.facingAngle = glm::degrees(std::atan2(center.z - s.pos.z, center.x - s.pos.x));
            s.occupiedBy = 0;
            allSeats.push_back(s);
        }
    }
}

void updateNPCs(float dt) {
    for(int i = 0; i < MAX_CUSTOMERS; ++i) {
        Customer& c = customers[i];
        if(!c.active) {
            if(rand() % 100 < 5) { // Increased spawn rate to 5% per frame
                c.active = true;
                c.pos = glm::vec3((rand()%3)-1.0f, 0.0f, 24.0f); 
                c.state = ENTERING;
                c.speed = 1.0f + (rand() % 10) * 0.1f;
                c.target = glm::vec3(0.0f, 0.0f, 1.8f); // Walkway Entrance
                c.assignedSeat = -1;
            }
            continue;
        }
        if(c.state == ENTERING || c.state == WALKING_TO_JUNCTION || c.state == WALKING_TO_SEAT || c.state == WALKING_OUT_JUNCTION || c.state == WALKING_OUT) {
            glm::vec3 dir = c.target - c.pos;
            dir.y = 0;
            if(glm::length(dir) < 0.2f) {
                if(c.state == ENTERING) {
                    std::vector<int> freeSeats;
                    for(int s = 0; s < allSeats.size(); ++s) if(allSeats[s].occupiedBy == 0) freeSeats.push_back(s);
                    if(!freeSeats.empty()) {
                        c.assignedSeat = freeSeats[rand() % freeSeats.size()];
                        allSeats[c.assignedSeat].occupiedBy = 2 + i;
                        c.target = glm::vec3(0.0f, 0.0f, -5.0f); // Central Aisle Junction
                        c.state = WALKING_TO_JUNCTION;
                    } else { c.target = glm::vec3(0.0f, 0.0f, 25.0f); c.state = WALKING_OUT; }
                }
                else if(c.state == WALKING_TO_JUNCTION) { c.target = allSeats[c.assignedSeat].pos; c.state = WALKING_TO_SEAT; }
                else if(c.state == WALKING_TO_SEAT) { c.pos = allSeats[c.assignedSeat].pos; c.state = SITTING; c.timer = 15.0f + (rand() % 20); }
                else if(c.state == WALKING_OUT_JUNCTION) { c.target = glm::vec3(0.0f, 0.0f, 25.0f); c.state = WALKING_OUT; }
                else if(c.state == WALKING_OUT) { c.active = false; }
            } else { c.pos += glm::normalize(dir) * c.speed * dt; }
        }
        else if(c.state == SITTING) {
            c.timer -= dt;
            if(c.timer <= 0) {
                allSeats[c.assignedSeat].occupiedBy = 0;
                c.assignedSeat = -1;
                c.target = glm::vec3(0.0f, 0.0f, -5.0f);
                c.state = WALKING_OUT_JUNCTION;
            }
        }
    }
}

void drawNPCs(Shader& ourShader, unsigned int vao, Cylinder& cylinder) {
    float t = glfwGetTime();

    for(int i = 0; i < MAX_CUSTOMERS; ++i) {
        if(!customers[i].active) continue;
        Customer& c = customers[i];
        
        float bodyH = 0.7f;
        float bodyY = (c.state == SITTING) ? 0.85f : 1.0f; // Center of torso height
        
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, glm::vec3(c.pos.x, bodyY, c.pos.z));
        
        // Orientation Phase
        if (c.state == SITTING) {
            m = glm::rotate(m, glm::radians(90.0f - allSeats[c.assignedSeat].facingAngle), glm::vec3(0, 1, 0));
        } else {
            glm::vec3 dir = c.target - c.pos;
            if (glm::length(dir) > 0.05f) {
                float angle = glm::degrees(std::atan2(dir.x, dir.z));
                m = glm::rotate(m, glm::radians(angle), glm::vec3(0, 1, 0));
            }
        }

        // Animation Phase
        float swing = 0.0f;
        if (c.state != SITTING && c.state != ENTERING && glm::length(c.target - c.pos) > 0.05f) {
            swing = sin(t * 12.0f) * 45.0f; // 45 degree walking swing
        } else if (c.state == ENTERING && glm::length(c.target - c.pos) > 0.05f) {
            swing = sin(t * 12.0f) * 45.0f;
        }

        ourShader.setBool("uUseTexture", false);

        // 1. Torso
        glm::mat4 torsoM = glm::scale(m, glm::vec3(0.35f, bodyH, 0.2f));
        ourShader.setMat4("model", torsoM);
        ourShader.setV4("baseColor", glm::vec4(0.2f, 0.3f, 0.6f + (i * 0.1f), 1.0f)); // Jacket
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        
        // 2. Head
        glm::mat4 headM = glm::translate(m, glm::vec3(0.0f, bodyH / 2.0f + 0.12f, 0.0f));
        headM = glm::scale(headM, glm::vec3(0.22f, 0.22f, 0.22f));
        ourShader.setMat4("model", headM);
        ourShader.setV4("baseColor", glm::vec4(0.85f, 0.7f, 0.55f, 1.0f)); // Skin tone
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // 3. Left Arm
        glm::mat4 laM = glm::translate(m, glm::vec3(-0.25f, bodyH / 2.0f - 0.05f, 0.0f)); // Shoulder pivot
        if(c.state == SITTING) laM = glm::rotate(laM, glm::radians(10.0f), glm::vec3(1, 0, 0)); // Resting arm
        else laM = glm::rotate(laM, glm::radians(-swing), glm::vec3(1, 0, 0)); // Swing
        laM = glm::translate(laM, glm::vec3(0.0f, -0.2f, 0.0f)); // Drop to arm center
        laM = glm::scale(laM, glm::vec3(0.12f, 0.4f, 0.12f));
        ourShader.setMat4("model", laM);
        ourShader.setV4("baseColor", glm::vec4(0.85f, 0.7f, 0.55f, 1.0f)); // Bare arms
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // 4. Right Arm
        glm::mat4 raM = glm::translate(m, glm::vec3(0.25f, bodyH / 2.0f - 0.05f, 0.0f)); // Shoulder pivot
        if(c.state == SITTING) raM = glm::rotate(raM, glm::radians(10.0f), glm::vec3(1, 0, 0));
        else raM = glm::rotate(raM, glm::radians(swing), glm::vec3(1, 0, 0));
        raM = glm::translate(raM, glm::vec3(0.0f, -0.2f, 0.0f));
        raM = glm::scale(raM, glm::vec3(0.12f, 0.4f, 0.12f));
        ourShader.setMat4("model", raM);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // 5. Left Leg
        glm::mat4 llM = glm::translate(m, glm::vec3(-0.1f, -bodyH / 2.0f, 0.0f)); // Hip pivot
        if(c.state == SITTING) llM = glm::rotate(llM, glm::radians(-85.0f), glm::vec3(1, 0, 0)); // 90deg lap sit
        else llM = glm::rotate(llM, glm::radians(swing), glm::vec3(1, 0, 0));
        llM = glm::translate(llM, glm::vec3(0.0f, -0.25f, 0.0f));
        llM = glm::scale(llM, glm::vec3(0.14f, 0.5f, 0.14f));
        ourShader.setMat4("model", llM);
        ourShader.setV4("baseColor", glm::vec4(0.15f, 0.15f, 0.15f, 1.0f)); // Slacks
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // 6. Right Leg
        glm::mat4 rlM = glm::translate(m, glm::vec3(0.1f, -bodyH / 2.0f, 0.0f)); // Hip pivot
        if(c.state == SITTING) rlM = glm::rotate(rlM, glm::radians(-85.0f), glm::vec3(1, 0, 0)); 
        else rlM = glm::rotate(rlM, glm::radians(-swing), glm::vec3(1, 0, 0));
        rlM = glm::translate(rlM, glm::vec3(0.0f, -0.25f, 0.0f));
        rlM = glm::scale(rlM, glm::vec3(0.14f, 0.5f, 0.14f));
        ourShader.setMat4("model", rlM);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
}
bool diffuseOn     = true;
bool specularOn    = true;

glm::vec3 canopyPointLights[] = {
    glm::vec3(-5.0f, 6.0f, -5.0f),
    glm::vec3( 5.0f, 6.0f, -5.0f),
    glm::vec3(-5.0f, 6.0f,  5.0f),
    glm::vec3( 5.0f, 6.0f,  5.0f)
};

// Texture Mapping State 
GLint wrapModes[] = { GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE };
int currentWrap = 0;
GLint filterModes[] = { GL_NEAREST, GL_LINEAR };
int currentFilter = 1;

// Textures
unsigned int woodTexture = 0, waterTexture = 0, canopyTexture = 0;

// ASSIGNMENT TOGGLES
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


// Cube Vertices (pos, normal, uv)
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


// Simple Cone (curvy object)
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


// Draw Cube (TEXTURE ENABLED)
void drawCube(Shader& shader, unsigned int vao,
    glm::vec3 pos, glm::vec3 scale,
    glm::vec4 color,
    unsigned int texID = 0)
{
    shader.setV4("baseColor", color);

    
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

// Cafe Objects (Realistic Curvy Objects)

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

// Stylized Table Set (textured)
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

    // Small "Curvy" Objects 
    applyTexModeToShader(shader);
    if (gTexMode != TEX_OFF) bindTex0(shader, waterTexture, 0); 
    else shader.setBool("uUseTexture", false);

    // Small Sphere 
    glm::mat4 sM = glm::mat4(1.0f);
    sM = glm::translate(sM, offset + glm::vec3(-0.2f, 1.03f, -0.2f));
    sM = glm::scale(sM, glm::vec3(0.15f * depthScale));
    shader.setMat4("model", sM);
    shader.setV4("baseColor", glm::vec4(0.9f, 0.7f, 0.3f, 1.0f));
    sphere.draw();

    // Small Tapered Object
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


// ================= PHASE 4 OBJECTS =================

void drawFan(Shader& shader, Cylinder& cylinder, unsigned int cubeVAO, glm::vec3 pos) {
    // Ceiling Rod
    glm::mat4 rModel = glm::mat4(1.0f);
    rModel = glm::translate(rModel, pos + glm::vec3(0, 0.4f, 0));
    rModel = glm::scale(rModel, glm::vec3(0.04f, 0.8f, 0.04f));
    shader.setMat4("model", rModel);
    shader.setV4("baseColor", glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));
    applyTexModeToShader(shader);
    shader.setBool("uUseTexture", false);
    cylinder.draw();

    // Hub
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, pos);
    model = glm::scale(model, glm::vec3(0.2f, 0.4f, 0.2f));
    shader.setMat4("model", model);
    shader.setV4("baseColor", glm::vec4(0.2f, 0.2f, 0.22f, 1.0f));
    applyTexModeToShader(shader);
    shader.setBool("uUseTexture", false);
    cylinder.draw();

    // Blades (Hierarchical to fanAngle)
    for (int i = 0; i < 4; i++) {
        glm::mat4 bModel = glm::mat4(1.0f);
        bModel = glm::translate(bModel, pos + glm::vec3(0, -0.1f, 0));
        bModel = glm::rotate(bModel, glm::radians(fanAngle + i * 90.0f), glm::vec3(0, 1, 0));
        bModel = glm::translate(bModel, glm::vec3(1.2f, 0, 0)); 
        bModel = glm::scale(bModel, glm::vec3(2.0f, 0.05f, 0.3f));
        shader.setMat4("model", bModel);
        shader.setV4("baseColor", glm::vec4(0.3f, 0.2f, 0.15f, 1.0f));
        
        applyTexModeToShader(shader);
        if (gTexMode != TEX_OFF) bindTex0(shader, woodTexture, 0); 
        else shader.setBool("uUseTexture", false);
        
        glBindVertexArray(cubeVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
}

void drawDoor(Shader& shader, unsigned int cubeVAO, glm::vec3 hingePos) {
    float doorWidth = 3.0f;
    float doorHeight = 4.8f;
    float doorThickness = 0.06f;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, hingePos); 
    model = glm::rotate(model, glm::radians(doorAngle), glm::vec3(0, 1, 0)); // Hinge rotation
    model = glm::translate(model, glm::vec3(doorWidth / 2.0f, doorHeight / 2.0f, 0)); 

    model = glm::scale(model, glm::vec3(doorWidth, doorHeight, doorThickness));
    shader.setMat4("model", model);
    shader.setV4("baseColor", glm::vec4(0.40f, 0.25f, 0.15f, 1.0f)); // Solid brown wood
    
    applyTexModeToShader(shader);
    shader.setBool("uUseTexture", false); // Enforce absolutely no transparent texture

    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void drawClock(Shader& shader, Cylinder& cylinder, unsigned int cubeVAO, glm::vec3 pos) {
    time_t t = time(0);
    struct tm* now = localtime(&t);
    
    float seconds = static_cast<float>(now->tm_sec);
    float minutes = now->tm_min + seconds / 60.0f; // fluid minute progression
    float hours = (now->tm_hour % 12) + minutes / 60.0f; // fluid hour progression

    float secAngle = -seconds * 6.0f;       
    float minAngle = -minutes * 6.0f;       
    float hrAngle = -hours * 30.0f;         

    // Flat Face
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, pos);
    // Cylinder draws natively along Z axis, leaving it unrotated makes the flat cap face +Z
    model = glm::scale(model, glm::vec3(1.2f, 1.2f, 0.1f));
    shader.setMat4("model", model);
    shader.setV4("baseColor", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)); // Deep black opaque body
    applyTexModeToShader(shader);
    shader.setBool("uUseTexture", false);
    cylinder.draw();
    
    // Abstract local hand drawer
    auto drawHand = [&](float angle, float width, float length, glm::vec4 color) {
        glm::mat4 hModel = glm::mat4(1.0f);
        hModel = glm::translate(hModel, pos + glm::vec3(0, 0, 0.08f));
        hModel = glm::rotate(hModel, glm::radians(angle), glm::vec3(0, 0, 1));
        hModel = glm::translate(hModel, glm::vec3(0, length / 2.0f, 0)); 
        hModel = glm::scale(hModel, glm::vec3(width, length, 0.02f));
        shader.setMat4("model", hModel);
        shader.setV4("baseColor", color);
        glBindVertexArray(cubeVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    };

    shader.setBool("uUseTexture", false);
    drawHand(hrAngle, 0.08f, 0.6f, glm::vec4(0.85f, 0.80f, 0.70f, 1.0f)); // Cream/Brass Hour
    drawHand(minAngle, 0.05f, 0.9f, glm::vec4(0.85f, 0.80f, 0.70f, 1.0f)); // Cream/Brass Min
    drawHand(secAngle, 0.02f, 1.0f, glm::vec4(0.85f, 0.3f, 0.3f, 1.0f)); // Soft Red Sec
}

void drawCoffeeMachine(Shader& shader, unsigned int cubeVAO, Cylinder& cylinder, glm::vec3 pos) {
    // Machine Chassis
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, pos);
    model = glm::scale(model, glm::vec3(1.0f, 1.2f, 0.8f));
    shader.setMat4("model", model);
    shader.setV4("baseColor", glm::vec4(0.65f, 0.70f, 0.75f, 1.0f));
    applyTexModeToShader(shader);
    shader.setBool("uUseTexture", false);
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    // Drip Tray Top
    model = glm::mat4(1.0f);
    model = glm::translate(model, pos + glm::vec3(0, 0.6f, 0.1f));
    model = glm::scale(model, glm::vec3(0.9f, 0.08f, 0.9f));
    shader.setMat4("model", model);
    shader.setV4("baseColor", glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    // Pull Lever (Hierarchical rotation off top-front)
    glm::vec3 hingePos = pos + glm::vec3(0.0f, 0.5f, 0.45f);
    
    glm::mat4 lModel = glm::mat4(1.0f);
    lModel = glm::translate(lModel, hingePos);
    lModel = glm::rotate(lModel, glm::radians(brewAngle), glm::vec3(1, 0, 0));
    lModel = glm::translate(lModel, glm::vec3(0, -0.2f, 0)); 
    lModel = glm::scale(lModel, glm::vec3(0.06f, 0.5f, 0.06f));
    shader.setMat4("model", lModel);
    shader.setV4("baseColor", glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
    cylinder.draw(); // simple cylinder acting as smooth arm

    // Spout Group
    glm::mat4 pModel = glm::mat4(1.0f);
    pModel = glm::translate(pModel, pos + glm::vec3(0.0f, -0.1f, 0.45f));
    pModel = glm::scale(pModel, glm::vec3(0.25f, 0.15f, 0.25f));
    shader.setMat4("model", pModel);
    shader.setV4("baseColor", glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
    cylinder.draw();
}

// Full Scene

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
        shader.setBool("uIsEmissiveObject", true);
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
        shader.setBool("uIsEmissiveObject", false);

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

    // ================= PHASE 4 SCENE ATTACHMENTS =================

    // 1. 3x Ceiling Fans (Anchored to the center of each of the 3 dining canopies)
    drawFan(shader, cylinder, cubeVAO, glm::vec3(-10.5f, 4.6f, -5.0f));
    drawFan(shader, cylinder, cubeVAO, glm::vec3( 0.0f,  4.6f, -20.0f));
    drawFan(shader, cylinder, cubeVAO, glm::vec3( 10.5f, 4.6f, -5.0f));

    // 2. Glass Door (Hinged tightly at the left frame of the entrance threshold)
    drawDoor(shader, cubeVAO, glm::vec3(-1.5f, 0.3f, 1.8f));

    // 3. Wall Clock (Mounted on the back glass wall, offset slightly to kill Z-fighting)
    drawClock(shader, cylinder, cubeVAO, glm::vec3(0.0f, 3.5f, -25.2f));

    // 4. Coffee Machine (Placed on the floor/table at the back wall)
    drawCoffeeMachine(shader, cubeVAO, cylinder, glm::vec3(0.0f, 1.2f, -19.0f));

}

// MAIN
int main()
{
    srand((unsigned int)time(NULL)); // Seed RNG
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

    // TEXTURES

    const char* tilePath   = "D:\\4-2\\Lab\\CSE 4208 Computer Graphics Laboratory\\Lab_4\\container2.png";
    const char* borderPath = "D:\\4-2\\Lab\\CSE 4208 Computer Graphics Laboratory\\Lab_4\\container2_specular.png";
    const char* emojiPath  = "D:\\4-2\\Lab\\CSE 4208 Computer Graphics Laboratory\\Lab_4\\emoji.png";

    woodTexture   = loadTexture(tilePath,   wrapModes[currentWrap], wrapModes[currentWrap], filterModes[currentFilter], filterModes[currentFilter]);
    canopyTexture = loadTexture(borderPath, wrapModes[currentWrap], wrapModes[currentWrap], filterModes[currentFilter], filterModes[currentFilter]);
    waterTexture  = loadTexture(emojiPath,  wrapModes[currentWrap], wrapModes[currentWrap], filterModes[currentFilter], filterModes[currentFilter]);

    std::cout << "\n============================================\n";
    std::cout << " CAFE BEEL HARINA - CONTROLS & INTERACTIONS \n";
    std::cout << "============================================\n";
    std::cout << " W/S/A/D : Move   | E/R : Up/Down Flight\n";
    std::cout << " X/Y/Z   : Rotate | F   : Drone Look-At\n";
    std::cout << " C       : View Cycle | H : Fullscreen\n";
    std::cout << " V       : Toggle 4-Way Viewport Mode\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " G       : Toggle Ceiling Fan (Phase 4)\n";
    std::cout << " K       : Toggle Entrance Door (Proximity)\n";
    std::cout << " B       : Toggle Coffee Machine (Proximity)\n";
    std::cout << " Q       : Sit/Stand at Table (Proximity)\n";
    std::cout << " I       : Toggle Walk/Free-Fly Mode\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " 1       : Sun (Directional Light)\n";
    std::cout << " 2       : Canopy Bulbs (Point Lights)\n";
    std::cout << " 3       : Flashlight (Spot Light)\n";
    std::cout << " 4       : Emissive Core Glow\n";
    std::cout << " 5/6/7   : Toggle Ambient/Diffuse/Specular\n";
    std::cout << " L       : MASTER LIGHTING BREAKER\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " 8/9/0/- : Texture Render Assignments\n";
    std::cout << " T / M   : Wrapping / Filtering Mods\n";
    std::cout << " P       : Wireframe Render Toggle\n";
    std::cout << "============================================\n\n";
    initSeats();
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // --- DYNAMIC PHASE 4 MATH ---
        if (isFanOn) {
            fanAngle += deltaTime * 200.0f;
            if (fanAngle >= 360.0f) fanAngle -= 360.0f;
        }
        float targetDoor = isDoorOpen ? -90.0f : 0.0f; // Open inwards/outwards (-90 is standard 90deg swing)
        doorAngle += (targetDoor - doorAngle) * deltaTime * 5.0f;

        float targetBrew = isBrewing ? -45.0f : 0.0f;
        brewAngle += (targetBrew - brewAngle) * deltaTime * 10.0f;

        updateNPCs(deltaTime);
        processInput(window);

        glClearColor(0.55f, 0.75f, 0.95f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ourShader.use();
        glPolygonMode(GL_FRONT_AND_BACK, isWireframe ? GL_LINE : GL_FILL);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (height == 0) height = 1;

        int halfW = width / 2;
        int halfH = height / 2;
        if (halfH == 0) halfH = 1;

        float aspect = (float)halfW / (float)halfH;
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), aspect, 0.1f, 100.0f);

        auto renderPass = [&](int vpX, int vpY, int vpW, int vpH, CameraMode evalMode, bool evaluateLookAtOverride = false) {
            glViewport(vpX, vpY, vpW, vpH);

            glm::vec3 effectiveViewPos = camera.Position;
            glm::vec3 effectiveViewFront = camera.Front;
            glm::mat4 view;

            if (evalMode == STATIC) {
                effectiveViewPos = glm::vec3(0, 10, 15);
                effectiveViewFront = glm::normalize(glm::vec3(0, 0, 0) - effectiveViewPos);
                view = glm::lookAt(effectiveViewPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
            }
            else if (evalMode == TOP) {
                effectiveViewPos = glm::vec3(0, 20, 0.1f);
                effectiveViewFront = glm::normalize(glm::vec3(0, 0, 0) - effectiveViewPos);
                view = glm::lookAt(effectiveViewPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
            }
            else if (evalMode == BIRD_EYE) {
                effectiveViewPos = glm::vec3(-15.0f, 15.0f, 15.0f);
                effectiveViewFront = glm::normalize(glm::vec3(0.0f, 0.0f, -2.0f) - effectiveViewPos);
                view = glm::lookAt(effectiveViewPos, glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(0, 1, 0));
            }
            else if (evalMode == ORBIT) {
                float radius = 15.0f;
                float camX = sin((float)glfwGetTime()) * radius;
                float camZ = cos((float)glfwGetTime()) * radius;
                effectiveViewPos = glm::vec3(camX, 5.0f, camZ);
                effectiveViewFront = glm::normalize(glm::vec3(0, 0, 0) - effectiveViewPos);
                view = glm::lookAt(effectiveViewPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
            }
            else { // FPS
                if (evaluateLookAtOverride && isLookAtOrbiting) {
                    float elapsedTime = (float)glfwGetTime() - orbitStartTime;
                    float angle = elapsedTime * 1.5f; 

                    glm::vec3 offset = orbitStartPos - orbitFocalPoint;
                    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
                    glm::vec3 spunPos = orbitFocalPoint + glm::vec3(rot * glm::vec4(offset, 0.0f));

                    effectiveViewPos = spunPos;
                    effectiveViewFront = glm::normalize(orbitFocalPoint - spunPos);
                    view = glm::lookAt(spunPos, orbitFocalPoint, glm::vec3(0.0f, 1.0f, 0.0f));
                } else {
                    effectiveViewPos = camera.Position;
                    effectiveViewFront = camera.Front;
                    view = camera.GetViewMatrix();
                }
            }

            // Update Illumination uniforms
            ourShader.setV3("viewPos", effectiveViewPos);
            ourShader.setBool("masterLightOn", masterLightOn);
            ourShader.setBool("dirLightOn", dirLightOn);
            ourShader.setBool("pointLightOn", pointLightOn);
            ourShader.setBool("spotLightOn", spotLightOn);
            ourShader.setBool("ambientOn", ambientOn);
            ourShader.setBool("diffuseOn", diffuseOn);
            ourShader.setBool("specularOn", specularOn);
            ourShader.setBool("emissiveOn", emissiveOn);

            glm::vec3 sunDir = glm::vec3(sin(glfwGetTime() * 0.5f), -1.0f, cos(glfwGetTime() * 0.5f));
            ourShader.setV3("dirLightDir", sunDir);
            ourShader.setV3("spotLightDir", effectiveViewFront);

            for(int i = 0; i < 4; i++) {
                ourShader.setV3("pointLightPositions[" + std::to_string(i) + "]", canopyPointLights[i]);
            }
            ourShader.setBool("uIsEmissiveObject", false);

            ourShader.setMat4("projection", projection);
            ourShader.setMat4("view", view);

            drawRiversideScene(ourShader, sphere, planter, cubeVAO, (float)glfwGetTime());
            drawNPCs(ourShader, cubeVAO, planter);

            // Phase 7: Carried Coffee HUD
            if (isWalkMode && hasCoffee && evalMode == FPS) {
                glClear(GL_DEPTH_BUFFER_BIT); // Ensure HUD always renders on top
                glm::mat4 hudModel = glm::inverse(view); // Stick to camera space natively
                hudModel = glm::translate(hudModel, glm::vec3(0.55f, -0.45f, -0.8f)); // Pushed deeper into bottom-right periphery
                hudModel = glm::rotate(hudModel, glm::radians(15.0f), glm::vec3(1.0f, 0.0f, 0.0f)); // Tip base slightly outward
                hudModel = glm::rotate(hudModel, glm::radians(-10.0f), glm::vec3(0.0f, 1.0f, 0.0f)); // Angle mug inwards
                hudModel = glm::scale(hudModel, glm::vec3(0.09f, 0.14f, 0.09f)); // Sleaker visual profile
                
                ourShader.setMat4("model", hudModel);
                ourShader.setV4("baseColor", glm::vec4(0.95f, 0.9f, 0.85f, 1.0f)); // Ceramic white mug
                ourShader.setBool("uUseTexture", false);
                ourShader.setInt("uComputeMode", 1);
                planter.draw();
            }
        };

        if (isWalkMode) {
            // Walk Mode rigorously enforces single FPS Viewport
            projection = glm::perspective(glm::radians(camera.Zoom), (float)width / (float)height, 0.1f, 100.0f);
            renderPass(0, 0, width, height, FPS, true);
        } else if (isFourWayView) {
            // Explicit 4-Quadrant Viewport Execution
            renderPass(0, halfH, halfW, halfH, BIRD_EYE);           // Top-Left
            renderPass(halfW, halfH, halfW, halfH, TOP);            // Top-Right
            renderPass(0, 0, halfW, halfH, STATIC);                 // Bottom-Left
            renderPass(halfW, 0, halfW, halfH, FPS, true);          // Bottom-Right (FPS override)
        } else {
            // Single Viewport Execution
            projection = glm::perspective(glm::radians(camera.Zoom), (float)width / (float)height, 0.1f, 100.0f);
            renderPass(0, 0, width, height, currentCameraMode, true);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &VBO);
    glfwTerminate();
    return 0;
}

// INPUT + CALLBACKS
void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    
    static bool keys[1024] = { false };

    glm::vec3 oldPos = camera.Position;

    if (!isSeated) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);

        if (!isWalkMode) {
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime);
            if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime);
        } else {
            // Apply locked height only for FPS mode in walk mode
            if (currentCameraMode == FPS) {
                camera.Position.y = 1.5f; 
            }
        }

        // Phase 6: Run bounds check
        if (!isPositionValid(camera.Position)) {
            camera.Position = oldPos;
        }
    }

    // Phase 6: Stateful Interaction Hints
    int currentZone = 0;
    int nearbySeatIdx = -1;
    if (horizontalDistanceXZ(camera.Position, glm::vec3(-1.5f, 0.0f, 1.8f)) < 2.5f) currentZone = 1;
    else if (horizontalDistanceXZ(camera.Position, glm::vec3(0.0f, 0.0f, -19.0f)) < 2.0f) currentZone = 2;
    else {
        float bestDist = 1.2f; 
        for(int s = 0; s < allSeats.size(); ++s) {
            if(allSeats[s].occupiedBy == 0 || allSeats[s].occupiedBy == 1) { 
                float d = horizontalDistanceXZ(camera.Position, allSeats[s].pos);
                if(d < bestDist) {
                    bestDist = d;
                    nearbySeatIdx = s;
                    currentZone = 3; 
                }
            }
        }
    }

    static int lastHintZone = 0;
    if (currentZone != lastHintZone) {
        if (currentZone == 1) std::cout << "\n[HINT] Press 'K' to interact with the Glass Door\n";
        else if (currentZone == 2) std::cout << "\n[HINT] Press 'B' to get Coffee\n";
        else if (currentZone == 3) std::cout << "\n[HINT] Press 'Q' to sit at a table\n";
        lastHintZone = currentZone;
    }

    if (!isWalkMode) {
        float pDir = 0, yDir = 0, rDir = 0;
        if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) pDir = 1.0f; 
        if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) yDir = 1.0f; 
        if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) rDir = 1.0f; 
        if (pDir != 0 || yDir != 0 || rDir != 0) camera.ProcessRotationKeys(pDir, yDir, rDir, deltaTime);
    }

    auto toggle = [&](int key, bool& val) {
        if (glfwGetKey(window, key) == GLFW_PRESS && !keys[key]) { val = !val; keys[key] = true; }
        else if (glfwGetKey(window, key) == GLFW_RELEASE) keys[key] = false;
    };

    toggle(GLFW_KEY_P, isWireframe);

    // Viewport Toggle
    if (!isWalkMode) {
        if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS && !keys[GLFW_KEY_V]) { isFourWayView = !isFourWayView; keys[GLFW_KEY_V] = true; std::cout << "Viewport Mode: " << (isFourWayView ? "4-Way" : "Single") << "\n"; }
        else if (glfwGetKey(window, GLFW_KEY_V) == GLFW_RELEASE) keys[GLFW_KEY_V] = false;
    } else {
        if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS && !keys[GLFW_KEY_V]) { keys[GLFW_KEY_V] = true; std::cout << "[LOCKED] Switch to Camera Viewer ('I') to use Viewports.\n"; }
        else if (glfwGetKey(window, GLFW_KEY_V) == GLFW_RELEASE) keys[GLFW_KEY_V] = false;
    }

    // Phase 4 & 5 Dynamic Toggles (Overridden with Phase 5 Proximity)
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !keys[GLFW_KEY_G]) { isFanOn = !isFanOn; keys[GLFW_KEY_G] = true; std::cout << "Ceiling Fan: " << (isFanOn ? "ON" : "OFF") << "\n"; }
    else if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE) keys[GLFW_KEY_G] = false;

    // --- Phase 5: Walk Mode Toggle (I) ---
    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS && !keys[GLFW_KEY_I]) { 
        isWalkMode = !isWalkMode; keys[GLFW_KEY_I] = true; 
        std::cout << "Walk Mode: " << (isWalkMode ? "ON" : "OFF (Free Fly)") << "\n"; 
        if (isWalkMode) {
            isFourWayView = false;
            isLookAtOrbiting = false;
            currentCameraMode = FPS;
            camera.Position.y = 1.5f;
            camera.Up = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
    else if (glfwGetKey(window, GLFW_KEY_I) == GLFW_RELEASE) keys[GLFW_KEY_I] = false;

    // --- Phase 5: Sit Interaction (Q) ---
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS && !keys[GLFW_KEY_Q]) {
        keys[GLFW_KEY_Q] = true;
        if (!isSeated) {
            if(currentZone == 3 && nearbySeatIdx != -1 && allSeats[nearbySeatIdx].occupiedBy == 0) {
                isSeated = true;
                playerSeatIdx = nearbySeatIdx;
                allSeats[nearbySeatIdx].occupiedBy = 1;
                Seat& s = allSeats[nearbySeatIdx];
                camera.Position = glm::vec3(s.pos.x, 1.1f, s.pos.z);
                camera.Yaw = s.facingAngle; 
                camera.Pitch = -15.0f; // Look gently downward at table plate
                camera.ProcessMouseMovement(0.0f, 0.0f, false); // Indirectly updateCameraVectors
                std::cout << "[INFO] Sat Down natively overlapping chair alignment.\n";
            }
        } else {
            isSeated = false;
            if(playerSeatIdx != -1) allSeats[playerSeatIdx].occupiedBy = 0;
            playerSeatIdx = -1;
            camera.Position.y = 1.5f;
            std::cout << "[INFO] Stood Up.\n";
        }
    }
    else if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_RELEASE) keys[GLFW_KEY_Q] = false;

    // --- Phase 5: Proximity Door Logic ---
    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS && !keys[GLFW_KEY_K]) { 
        keys[GLFW_KEY_K] = true; 
        if (horizontalDistanceXZ(camera.Position, glm::vec3(-1.5f, 0.0f, 1.8f)) < 3.0f) {
            isDoorOpen = !isDoorOpen; 
            std::cout << "Glass Door: " << (isDoorOpen ? "OPENING" : "CLOSING") << "\n"; 
        } else {
            std::cout << "Move closer to the door\n";
        }
    }
    else if (glfwGetKey(window, GLFW_KEY_K) == GLFW_RELEASE) keys[GLFW_KEY_K] = false;

    // --- Phase 5: Proximity Coffee Machine ---
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS && !keys[GLFW_KEY_B]) { 
        keys[GLFW_KEY_B] = true; 
        if (horizontalDistanceXZ(camera.Position, glm::vec3(0.0f, 0.0f, -19.0f)) < 3.0f) {
            isBrewing = !isBrewing;
            if (isBrewing) {
                if (!hasCoffee) {
                    hasCoffee = true;
                    std::cout << "Coffee Machine: Brewing started. Coffee collected!\n";
                } else {
                    std::cout << "Coffee Machine: Brewing started. (You already have coffee)\n";
                }
            } else {
                std::cout << "Coffee Machine: Brewing paused/stopped.\n";
            }
        } else {
            std::cout << "Move closer to the coffee machine\n";
        }
    }
    else if (glfwGetKey(window, GLFW_KEY_B) == GLFW_RELEASE) keys[GLFW_KEY_B] = false;

    // Texture Map Logic
    if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS && !keys[GLFW_KEY_8]) { gTexMode = TEX_OFF; keys[GLFW_KEY_8] = true; std::cout << "TEX_OFF\n"; }
    else if (glfwGetKey(window, GLFW_KEY_8) == GLFW_RELEASE) keys[GLFW_KEY_8] = false;

    if (glfwGetKey(window, GLFW_KEY_9) == GLFW_PRESS && !keys[GLFW_KEY_9]) { gTexMode = TEX_SIMPLE; keys[GLFW_KEY_9] = true; std::cout << "TEX_SIMPLE\n"; }
    else if (glfwGetKey(window, GLFW_KEY_9) == GLFW_RELEASE) keys[GLFW_KEY_9] = false;

    if (glfwGetKey(window, GLFW_KEY_0) == GLFW_PRESS && !keys[GLFW_KEY_0]) { gTexMode = TEX_BLEND_VERTEX; keys[GLFW_KEY_0] = true; std::cout << "TEX_BLEND_VERTEX\n"; }
    else if (glfwGetKey(window, GLFW_KEY_0) == GLFW_RELEASE) keys[GLFW_KEY_0] = false;

    if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS && !keys[GLFW_KEY_MINUS]) { gTexMode = TEX_BLEND_FRAGMENT; keys[GLFW_KEY_MINUS] = true; std::cout << "TEX_BLEND_FRAGMENT\n"; }
    else if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_RELEASE) keys[GLFW_KEY_MINUS] = false;

    // Lighting Control Logic
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS && !keys[GLFW_KEY_1]) { dirLightOn = !dirLightOn; keys[GLFW_KEY_1] = true; std::cout << "Sun (Directional): " << dirLightOn << "\n";}
    else if (glfwGetKey(window, GLFW_KEY_1) == GLFW_RELEASE) keys[GLFW_KEY_1] = false;

    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS && !keys[GLFW_KEY_2]) { pointLightOn = !pointLightOn; keys[GLFW_KEY_2] = true; std::cout << "Canopy Bulbs (Point): " << pointLightOn << "\n";}
    else if (glfwGetKey(window, GLFW_KEY_2) == GLFW_RELEASE) keys[GLFW_KEY_2] = false;

    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS && !keys[GLFW_KEY_3]) { spotLightOn = !spotLightOn; keys[GLFW_KEY_3] = true; std::cout << "Flashlight (Spot): " << spotLightOn << "\n";}
    else if (glfwGetKey(window, GLFW_KEY_3) == GLFW_RELEASE) keys[GLFW_KEY_3] = false;

    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS && !keys[GLFW_KEY_4]) { emissiveOn = !emissiveOn; keys[GLFW_KEY_4] = true; std::cout << "Emissive Glow: " << emissiveOn << "\n";}
    else if (glfwGetKey(window, GLFW_KEY_4) == GLFW_RELEASE) keys[GLFW_KEY_4] = false;

    if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS && !keys[GLFW_KEY_5]) { ambientOn = !ambientOn; keys[GLFW_KEY_5] = true; std::cout << "Ambient Bounce: " << ambientOn << "\n";}
    else if (glfwGetKey(window, GLFW_KEY_5) == GLFW_RELEASE) keys[GLFW_KEY_5] = false;

    if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS && !keys[GLFW_KEY_6]) { diffuseOn = !diffuseOn; keys[GLFW_KEY_6] = true; std::cout << "Diffuse Map: " << diffuseOn << "\n";}
    else if (glfwGetKey(window, GLFW_KEY_6) == GLFW_RELEASE) keys[GLFW_KEY_6] = false;

    if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS && !keys[GLFW_KEY_7]) { specularOn = !specularOn; keys[GLFW_KEY_7] = true; std::cout << "Specular Gloss: " << specularOn << "\n";}
    else if (glfwGetKey(window, GLFW_KEY_7) == GLFW_RELEASE) keys[GLFW_KEY_7] = false;

    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS && !keys[GLFW_KEY_L]) { masterLightOn = !masterLightOn; keys[GLFW_KEY_L] = true; std::cout << "MASTER LIGHT SWITCH: " << masterLightOn << "\n";}
    else if (glfwGetKey(window, GLFW_KEY_L) == GLFW_RELEASE) keys[GLFW_KEY_L] = false;

    // Fullscreen Toggle (H)
    static bool isFullscreen = false;
    if (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS && !keys[GLFW_KEY_H]) {
        isFullscreen = !isFullscreen;
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (isFullscreen) glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        else glfwSetWindowMonitor(window, NULL, 100, 100, SCR_WIDTH, SCR_HEIGHT, 0);
        keys[GLFW_KEY_H] = true;
    }
    else if (glfwGetKey(window, GLFW_KEY_H) == GLFW_RELEASE) keys[GLFW_KEY_H] = false;

    // Look-at Orbit Toggle (F)
    if (!isWalkMode) {
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !keys[GLFW_KEY_F]) {
            isLookAtOrbiting = !isLookAtOrbiting;
            if (isLookAtOrbiting) {
                orbitStartTime = (float)glfwGetTime();
                orbitFocalPoint = camera.Position + (camera.Front * 5.0f);
                orbitStartPos = camera.Position;
            }
            std::cout << "> Focal Pivot Drone Mode: " << (isLookAtOrbiting ? "ON\n" : "OFF\n");
            keys[GLFW_KEY_F] = true;
        }
        else if (glfwGetKey(window, GLFW_KEY_F) == GLFW_RELEASE) keys[GLFW_KEY_F] = false;
    } else {
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !keys[GLFW_KEY_F]) { keys[GLFW_KEY_F] = true; std::cout << "[LOCKED] Switch to Camera Viewer ('I') to use Orbit Drone.\n"; }
        else if (glfwGetKey(window, GLFW_KEY_F) == GLFW_RELEASE) keys[GLFW_KEY_F] = false;
    }

    // cycle camera (C)
    if (!isWalkMode) {
        if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && !keys[GLFW_KEY_C]) {
            currentCameraMode = (CameraMode)((currentCameraMode + 1) % 5);
            keys[GLFW_KEY_C] = true;
        }
        else if (glfwGetKey(window, GLFW_KEY_C) == GLFW_RELEASE) keys[GLFW_KEY_C] = false;
    } else {
        if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && !keys[GLFW_KEY_C]) { keys[GLFW_KEY_C] = true; std::cout << "[LOCKED] Switch to Camera Viewer ('I') to Cycle Views.\n"; }
        else if (glfwGetKey(window, GLFW_KEY_C) == GLFW_RELEASE) keys[GLFW_KEY_C] = false;
    }

    // wrapping (T)
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !keys[GLFW_KEY_T]) {
        currentWrap = (currentWrap + 1) % 3;
        unsigned int texs[] = { woodTexture, waterTexture, canopyTexture };
        for (auto t : texs) {
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapModes[currentWrap]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapModes[currentWrap]);
        }
        keys[GLFW_KEY_T] = true;
        std::cout << "Wrap mode changed\n";
    }
    else if (glfwGetKey(window, GLFW_KEY_T) == GLFW_RELEASE) keys[GLFW_KEY_T] = false;

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

// TEXTURE LOADER

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


// ALWAYS-VALID SOLID TEXTURE
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