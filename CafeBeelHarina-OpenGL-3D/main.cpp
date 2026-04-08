#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Camera.h"
#include "Cylinder.h"
#include "Shader.h"
#include "Sphere.h"
#include "stb_image.h"

#include <cmath>
#include <ctime>
#include <iostream>
#include <vector>

// Function Prototypes
void framebuffer_size_callback(GLFWwindow *window, int width, int height);
void mouse_callback(GLFWwindow *window, double xpos, double ypos);
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);

// ASSIGNMENT TOGGLES
enum TexFeatureMode {
  TEX_OFF = 0,           // no texture (baseColor only)
  TEX_SIMPLE = 1,        // simple texture (no surface color)
  TEX_BLEND_VERTEX = 2,  // blended with baseColor, computed on VERTEX shader
  TEX_BLEND_FRAGMENT = 3 // blended with baseColor, computed on FRAGMENT shader
};
TexFeatureMode gTexMode = TEX_OFF;

// Texture Mapping State
GLint wrapModes[] = {GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE};
int currentWrap = 0;
GLint filterModes[] = {GL_NEAREST, GL_LINEAR};
int currentFilter = 1;

// Textures - Scope corrected to Top Level
unsigned int woodTexture = 0, waterTexture = 0, canopyTexture = 0;

// Apply current mode to shader uniforms
static void applyTexModeToShader(Shader &shader) {
  if (gTexMode == TEX_OFF) {
    shader.setBool("uUseTexture", false);
    shader.setBool("uBlendWithColor", false);
    shader.setInt("uComputeMode", 1);
  } else if (gTexMode == TEX_SIMPLE) {
    shader.setBool("uUseTexture", true);
    shader.setBool("uBlendWithColor", false);
    shader.setInt("uComputeMode", 1); // fragment is fine
  } else if (gTexMode == TEX_BLEND_VERTEX) {
    shader.setBool("uUseTexture", true);
    shader.setBool("uBlendWithColor", true);
    shader.setInt("uComputeMode", 0); // vertex computed
  } else {                            // TEX_BLEND_FRAGMENT
    shader.setBool("uUseTexture", true);
    shader.setBool("uBlendWithColor", true);
    shader.setInt("uComputeMode", 1); // fragment computed
  }
}

static void bindTex0(Shader &shader, unsigned int texID, int unit = 0) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, texID);
  shader.setInt("uTex0", unit);
}


// --- Phase 1: CUSTOM MATH & BEZIER EVALUATION ---
glm::mat4 customRotate(glm::mat4 m, float angle, glm::vec3 axis) {
  float a = angle;
  float c = (float)cos(a);
  float s = (float)sin(a);
  axis = glm::normalize(axis);
  glm::vec3 temp = (1.0f - c) * axis;

  glm::mat4 Rotate;
  Rotate[0][0] = c + temp[0] * axis[0];
  Rotate[0][1] = temp[0] * axis[1] + s * axis[2];
  Rotate[0][2] = temp[0] * axis[2] - s * axis[1];
  Rotate[0][3] = 0.0f;

  Rotate[1][0] = temp[1] * axis[0] - s * axis[2];
  Rotate[1][1] = c + temp[1] * axis[1];
  Rotate[1][2] = temp[1] * axis[2] + s * axis[0];
  Rotate[1][3] = 0.0f;

  Rotate[2][0] = temp[2] * axis[0] + s * axis[1];
  Rotate[2][1] = temp[2] * axis[1] - s * axis[0];
  Rotate[2][2] = c + temp[2] * axis[2];
  Rotate[2][3] = 0.0f;

  Rotate[3][0] = 0.0f;
  Rotate[3][1] = 0.0f;
  Rotate[3][2] = 0.0f;
  Rotate[3][3] = 1.0f;

  glm::mat4 Result;
  Result[0] = m[0] * Rotate[0][0] + m[1] * Rotate[0][1] + m[2] * Rotate[0][2];
  Result[1] = m[0] * Rotate[1][0] + m[1] * Rotate[1][1] + m[2] * Rotate[1][2];
  Result[2] = m[0] * Rotate[2][0] + m[1] * Rotate[2][1] + m[2] * Rotate[2][2];
  Result[3] = m[3];
  return Result;
}

glm::vec2 bezierIterate(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                        float t) {
  float u = 1.0f - t;
  float tt = t * t;
  float uu = u * u;
  float uuu = uu * u;
  float ttt = tt * t;

  glm::vec2 p = uuu * p0;
  p += 3 * uu * t * p1;
  p += 3 * u * tt * p2;
  p += ttt * p3;
  return p;
}

void buildSurfaceOfRevolution(std::vector<glm::vec2> &controlPoints,
                              int segments, GLuint &vao, GLuint &vbo,
                              GLuint &ebo, int &indexCount,
                              bool useBezier = false) {
  std::vector<glm::vec2> profile;
  if (useBezier && controlPoints.size() >= 4) {
    for (int i = 0; i < (int)controlPoints.size() - 3; i += 3) {
      for (int j = 0; j <= 20; j++) {
        float t = (float)j / 20.0f;
        if (j == 0 && i > 0)
          continue;
        profile.push_back(bezierIterate(controlPoints[i], controlPoints[i + 1],
                                        controlPoints[i + 2],
                                        controlPoints[i + 3], t));
      }
    }

  } else {
    profile = controlPoints;
  }


  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  float angleStep = 2.0f * 3.14159265f / segments;

  for (int i = 0; i <= segments; ++i) {
    float angle = i * angleStep;
    float c = cos(angle);
    float s = sin(angle);

    for (int j = 0; j < (int)profile.size(); ++j) {

      float x = profile[j].x * c;
      float z = profile[j].x * s;
      float y = profile[j].y;

      // Normal Calculation
      float nx = c;
      float nz = s;
      float ny = 0.0f;
      if (j > 0 && j < (int)profile.size() - 1) {

        glm::vec2 tangent = profile[j + 1] - profile[j - 1];
        glm::vec2 normal = glm::normalize(glm::vec2(-tangent.y, tangent.x));
        nx = normal.x * c;
        nz = normal.x * s;
        ny = normal.y;
      }

      vertices.push_back(x);
      vertices.push_back(y);
      vertices.push_back(z);
      vertices.push_back(nx);
      vertices.push_back(ny);
      vertices.push_back(nz);
      vertices.push_back((float)i / (float)segments);
      vertices.push_back((float)j / (float)(profile.size() - 1));

    }
  }

  for (int i = 0; i < segments; ++i) {
    for (int j = 0; j < (int)profile.size() - 1; ++j) {
      int current = (int)(i * profile.size() + j);
      int next = (int)((i + 1) * profile.size() + j);
      indices.push_back((unsigned int)current);
      indices.push_back((unsigned int)next);
      indices.push_back((unsigned int)current + 1);
      indices.push_back((unsigned int)next);
      indices.push_back((unsigned int)next + 1);
      indices.push_back((unsigned int)current + 1);
    }
  }


  indexCount = (int)indices.size();

  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
               vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glBindVertexArray(0);
}

// Global Geometry Pointers for Bezier objects
GLuint coffeeVAO, coffeeVBO, coffeeEBO;
int coffeeIndexCount = 0;

struct Bird {
  glm::vec3 pos;
  float speed;
  float flapOffset;
  float courseAngle;
};
std::vector<Bird> skyBirds;

// --- Phase 1B: RULED SURFACE & PROCEDURAL GENERATORS ---
GLuint ruledVAO, ruledVBO, ruledEBO;
int ruledIndexCount = 0;
GLuint splineVAO, splineVBO;
int splineVertexCount = 0;

void buildRuledSurface() {
  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  int uSteps = 20;
  int vSteps = 10;

  // curve 1: Bottom arch
  auto getC1 = [](float u) -> glm::vec3 {
    float theta = u * 3.14159f;
    return glm::vec3(cos(theta) * 3.0f, sin(theta) * 3.0f, -0.5f);
  };
  // curve 2: Top arch (wider roof)
  auto getC2 = [](float u) -> glm::vec3 {
    float theta = u * 3.14159f;
    return glm::vec3(cos(theta) * 3.5f, sin(theta) * 3.5f, 0.5f);
  };

  for (int i = 0; i <= vSteps; ++i) {
    float v = (float)i / (float)vSteps;
    for (int j = 0; j <= uSteps; ++j) {
      float u = (float)j / (float)uSteps;

      glm::vec3 p = (1.0f - v) * getC1(u) + v * getC2(u);

      // Calculate normal roughly
      glm::vec3 tanU = (1.0f - v) * (getC1(u + 0.01f) - getC1(u)) +
                       v * (getC2(u + 0.01f) - getC2(u));
      glm::vec3 tanV = getC2(u) - getC1(u);
      glm::vec3 norm = glm::normalize(glm::cross(tanU, tanV));

      vertices.push_back(p.x);
      vertices.push_back(p.y);
      vertices.push_back(p.z);
      vertices.push_back(norm.x);
      vertices.push_back(norm.y);
      vertices.push_back(norm.z);
      vertices.push_back(u);
      vertices.push_back(v);
    }
  }

  for (int i = 0; i < vSteps; ++i) {
    for (int j = 0; j < uSteps; ++j) {
      int c = i * (uSteps + 1) + j;
      int n = (i + 1) * (uSteps + 1) + j;
      indices.push_back(c);
      indices.push_back(n);
      indices.push_back(c + 1);
      indices.push_back(n);
      indices.push_back(n + 1);
      indices.push_back(c + 1);
    }
  }

  ruledIndexCount = (int)indices.size();

  glGenVertexArrays(1, &ruledVAO);
  glGenBuffers(1, &ruledVBO);
  glGenBuffers(1, &ruledEBO);
  glBindVertexArray(ruledVAO);
  glBindBuffer(GL_ARRAY_BUFFER, ruledVBO);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
               vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ruledEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glBindVertexArray(0);
}

// --- Spline Evaluation ---
glm::vec3 catmullRom(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                     float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                 (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void buildSplinePathVAO(std::vector<glm::vec3> &ctrl, int steps, GLuint &vao,
                        GLuint &vbo, int &vertexCount) {
  std::vector<float> vertices;
  if (ctrl.size() < 4)
    return;

  for (int i = 0; i < (int)ctrl.size() - 3; ++i) {

    for (int j = 0; j <= steps; ++j) {
      float t = (float)j / (float)steps;
      glm::vec3 p =
          catmullRom(ctrl[i], ctrl[i + 1], ctrl[i + 2], ctrl[i + 3], t);
      vertices.push_back(p.x);
      vertices.push_back(p.y);
      vertices.push_back(p.z);
      vertices.push_back(0.0f);
      vertices.push_back(1.0f);
      vertices.push_back(0.0f); // Normal
      vertices.push_back(t);
      vertices.push_back(0.0f); // UV
    }
  }

  vertexCount = (int)(vertices.size() / 8);

  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
               vertices.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
}

void initGeometricAssets() {
  // 1. Coffee Glass (Bezier profile)
  std::vector<glm::vec2> glassControlPoints = {
      glm::vec2(0.0f, 0.0f),   glm::vec2(0.25f, 0.0f),
      glm::vec2(0.25f, 0.05f), glm::vec2(0.15f, 0.1f), // Base
      glm::vec2(0.15f, 0.1f),  glm::vec2(0.18f, 0.3f),
      glm::vec2(0.2f, 0.5f),   glm::vec2(0.35f, 0.7f), // Body lower
      glm::vec2(0.35f, 0.7f),  glm::vec2(0.35f, 0.75f),
      glm::vec2(0.35f, 0.8f),  glm::vec2(0.36f, 0.9f) // Lip
  };
  buildSurfaceOfRevolution(glassControlPoints, 32, coffeeVAO, coffeeVBO,
                           coffeeEBO, coffeeIndexCount, true);

  // 2. Ruled Surface Arch
  buildRuledSurface();

  // 3. Spline Path (Decorative Sign Support)
  std::vector<glm::vec3> signCtrl = {
      glm::vec3(-2.0f, 4.0f, 2.0f), glm::vec3(-1.0f, 5.0f, 2.0f),
      glm::vec3(1.0f, 5.0f, 2.0f),  glm::vec3(2.0f, 4.0f, 2.0f),
      glm::vec3(1.0f, 3.0f, 2.0f),  glm::vec3(0.0f, 3.5f, 2.0f)};
  buildSplinePathVAO(signCtrl, 20, splineVAO, splineVBO, splineVertexCount);

  // 4. Initialize Birds
  for (int i = 0; i < 8; ++i) {
    Bird b;
    b.pos = glm::vec3(-20.0f + (rand() % 40), 10.0f + (rand() % 5),
                      -20.0f + (rand() % 40)); // Constrained higher
    b.speed = 1.0f + (rand() % 15) * 0.1f;
    b.flapOffset = (rand() % 100) * 0.1f;
    b.courseAngle = (float)(rand() % 360);

    skyBirds.push_back(b);
  }
}

// Texture utils
unsigned int loadTexture(char const *path, GLint wrapS = GL_REPEAT,
                         GLint wrapT = GL_REPEAT, GLint minFilter = GL_LINEAR,
                         GLint magFilter = GL_LINEAR);

unsigned int createSolidTextureRGBA(unsigned char r, unsigned char g,
                                    unsigned char b, unsigned char a);

// Settings
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 900;

// Phase 5 Player State
bool isWalkMode = true;
bool isSeated = false;
glm::vec3 preSeatPos = glm::vec3(0.0f);
bool hasCoffee = false;
enum ServingState {
  IDLE,
  MOVE_TO_PICKUP,
  GRIP,
  MOVE_TO_POUR,
  MOVE_TO_HANDOFF,
  RELEASE,
  HOME
};
ServingState robotState = IDLE;
int targetNPCIdx = -1; // -1 for player

// Day/Night Cycle
float dayCycleTime = 25.0f; // Start at Morning
const float DAY_DURATION = 100.0f;
glm::vec3 worldSunDir = glm::vec3(0.0f);
glm::vec3 worldSunColor = glm::vec3(1.0f);
float worldAmbientIntensity = 0.25f;

// Window State
bool isWindowOpen = false;
float windowAngle = 0.0f;

float horizontalDistanceXZ(glm::vec3 p1, glm::vec3 p2) {
  return glm::length(glm::vec2(p1.x - p2.x, p1.z - p2.z));
}

// Camera
Camera camera(glm::vec3(0.0f, 2.55f, 24.0f));

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
bool playerWantsDoorOpen = false; // Player-initiated door open via K
bool isBrewing = false;
float brewAngle = 0.0f;
float stateTimer = 0.0f;
bool signLightOn = true; // Cafe sign glow toggle
float doorSwingDirection = 1.0f; // 1.0 = Outward (CCW), -1.0 = Inward (CW)
float doorAutoCloseTimer = 0.0f; // Reset when door is closed



// Phase 6: AABB Collision Helper
bool isPositionValid(glm::vec3 pos) {
  if (!isWalkMode)
    return true; // Disabled during Free Fly

  float x = pos.x;
  float z = pos.z;

  // Walkway Limits (Narrowed to 1.45f to block glass rails fluidly)
  bool onWalkway = (x >= -1.45f && x <= 1.45f && z >= 1.5f && z <= 25.0f);
  // Main Dining Floor Limits
  bool onMainFloor = (x >= -20.0f && x <= 20.0f && z >= -13.0f && z <= 2.0f);
  // Back Dining Room Limits
  bool onBackFloor = (x >= -7.0f && x <= 7.0f && z >= -26.0f && z <= -13.0f);

  if (!onWalkway && !onMainFloor && !onBackFloor)
    return false;

  // Door Bound Extents
  if (!isDoorOpen) {
    if (x >= -1.6f && x <= 1.6f && z >= 1.6f && z <= 2.0f)
      return false; // Relaxed Z-depth for tighter opening distance
  }

  // Phase 7: Interior Collision
  // Front Glass Wall Divider Blocks (stops sprinting past the closed door
  // bounds)
  if (z > 1.6f && z < 2.0f) {
    if (x < -1.5f || x > 1.5f)
      return false;
  }

  // Phase 7: Table & Machine Obstructions
  if (horizontalDistanceXZ(pos, glm::vec3(0.0f, 0.0f, -19.0f)) < 1.0f)
    return false; // Coffee Machine base

  glm::vec3 tables[] = {
      glm::vec3(-3.2f, 0, -20.0f), glm::vec3(3.2f, 0, -20.0f), // Back Canopy
      glm::vec3(-14.3f, 0, -8.2f), glm::vec3(-14.3f, 0, -1.8f),
      glm::vec3(-6.7f, 0, -8.2f),  glm::vec3(-6.7f, 0, -1.8f), // Left Canopy
      glm::vec3(6.7f, 0, -8.2f),   glm::vec3(6.7f, 0, -1.8f),
      glm::vec3(14.3f, 0, -8.2f),  glm::vec3(14.3f, 0, -1.8f) // Right Canopy
  };

  for (int i = 0; i < 10; ++i) {
    // Precise AABB boundary matching the exact dimensions of 1 table + 4
    // chairs. Relaxed marginally to 1.9f (X) and 1.8f (Z) to allow extremely
    // fluid movement between chairs without feeling rigid
    if (std::abs(x - tables[i].x) < 1.9f && std::abs(z - tables[i].z) < 1.8f)
      return false;
  }

  // Inner Glass Walls boundaries (explicit blocking inside canopies)
  if (std::abs(z - (-11.8f)) < 0.2f) {
    if (x < -1.5f || x > 1.5f)
      return false; // Main canopy back dividers
  }
  if (z > -26.0f && z < -14.0f) {
    if (std::abs(x - (-6.5f)) < 0.2f)
      return false; // Side glass back dining
    if (std::abs(x - 6.5f) < 0.2f)
      return false; // Side glass back dining
  }

  return true;
}

// Scene State
bool emissiveOn = false;
bool isWireframe = false;
bool isFourWayView = false;

// 7. Lighting Struct State Globals
bool masterLightOn = true;
bool dirLightOn = true;
bool pointLightOn = true;
bool spotLightOn = true;
bool ambientOn = true;

// Phase 8: NPCs and Seating
struct Seat {
  glm::vec3 pos;
  float facingAngle;
  int occupiedBy; // 0=Empty, 1=Player, 2+=NPC
};
std::vector<Seat> allSeats;
int playerSeatIdx = -1;

enum NPCState {
  ENTERING,
  WALKING_TO_JUNCTION,
  WALKING_TO_MACHINE,
  WAITING_FOR_COFFEE,
  WALKING_TO_SEAT,
  SITTING,
  WALKING_OUT_JUNCTION,
  WALKING_OUT
};
struct Customer {
  glm::vec3 pos;
  glm::vec3 target;
  NPCState state;
  int assignedSeat;
  float timer;
  float speed;
  bool active;
  bool hasReceivedCoffee; // true after robot delivers coffee
  int clothingSeed;       // randomized outfit seed per NPC
};
#define MAX_CUSTOMERS 3
Customer customers[MAX_CUSTOMERS];

void initSeats() {
  glm::vec3 hintTables[] = {
      glm::vec3(-3.2f, 0, -20.0f), glm::vec3(3.2f, 0, -20.0f),
      glm::vec3(-14.3f, 0, -8.2f), glm::vec3(-14.3f, 0, -1.8f),
      glm::vec3(-6.7f, 0, -8.2f),  glm::vec3(-6.7f, 0, -1.8f),
      glm::vec3(6.7f, 0, -8.2f),   glm::vec3(6.7f, 0, -1.8f),
      glm::vec3(14.3f, 0, -8.2f),  glm::vec3(14.3f, 0, -1.8f)};
  for (int i = 0; i < 10; ++i) {
    glm::vec3 center = hintTables[i];
    float rot = 5.0f * sin(center.x * 0.5f + center.z * 0.3f);
    glm::vec3 chairPos[] = {
        glm::vec3(0.0f, 0.0f, 1.6f), glm::vec3(0.0f, 0.0f, -1.6f),
        glm::vec3(1.7f, 0.0f, 0.0f), glm::vec3(-1.7f, 0.0f, 0.0f)};
    for (int c = 0; c < 4; ++c) {
      glm::mat4 model = glm::mat4(1.0f);
      model = glm::translate(model, center);
      model = glm::rotate(model, glm::radians(rot), glm::vec3(0, 1, 0));
      glm::vec4 cPos = model * glm::vec4(chairPos[c], 1.0f);
      Seat s;
      s.pos = glm::vec3(cPos);
      s.facingAngle =
          glm::degrees(std::atan2(center.z - s.pos.z, center.x - s.pos.x));
      s.occupiedBy = 0;
      allSeats.push_back(s);
    }
  }
}

void updateNPCs(float dt) {
  glm::vec3 queueSpots[] = {glm::vec3(1.0f, 0.0f, -16.5f),
                            glm::vec3(-1.0f, 0.0f, -16.5f)};

  for (int i = 0; i < MAX_CUSTOMERS; ++i) {
    Customer &c = customers[i];
    if (!c.active) {
      if (rand() % 100 < 5) {
        c.active = true;
        c.pos = glm::vec3((rand() % 3) - 1.0f, 0.0f, 24.0f);
        c.state = ENTERING;
        c.speed = 1.0f + (rand() % 10) * 0.1f;
        c.target = glm::vec3(0.0f, 0.0f, 1.8f);
        c.assignedSeat = -1;
        c.hasReceivedCoffee = false;
        c.clothingSeed = rand();
      }
      continue;
    }

    // --- NPC Avoidance (Smoothed & Capped Force) ---
    if (c.state != SITTING && c.state != WAITING_FOR_COFFEE) {
      for (int j = 0; j < MAX_CUSTOMERS; ++j) {
        if (i == j || !customers[j].active || customers[j].state == SITTING)
          continue;
        float dist = glm::distance(c.pos, customers[j].pos);
        if (dist < 0.9f) {
          glm::vec3 push = glm::normalize(c.pos - customers[j].pos);
          // Capped, gentle smoothing: displacement is proportional to overlap
          // but capped
          float overlap = 0.9f - dist;
          c.pos += push * overlap * dt * 2.0f;
        }
      }
      // Player avoidance (gentle)
      float pDist = glm::distance(c.pos, camera.Position);
      if (pDist < 0.9f) {
        glm::vec3 pPush = glm::normalize(c.pos - camera.Position);
        c.pos += pPush * (0.9f - pDist) * dt * 2.0f;
      }
    }

    if (c.state == ENTERING || c.state == WALKING_TO_JUNCTION ||
        c.state == WALKING_TO_MACHINE || c.state == WALKING_TO_SEAT ||
        c.state == WALKING_OUT_JUNCTION || c.state == WALKING_OUT) {
      glm::vec3 dir = c.target - c.pos;
      dir.y = 0;
      if (glm::length(dir) < 0.2f) {
        if (c.state == ENTERING) {
          std::vector<int> freeSeats;
          for (int s = 0; s < (int)allSeats.size(); ++s)
            if (allSeats[s].occupiedBy == 0)
              freeSeats.push_back(s);
          if (!freeSeats.empty()) {
            c.assignedSeat = freeSeats[rand() % freeSeats.size()];
            allSeats[c.assignedSeat].occupiedBy = 2 + i;
            if (rand() % 2 == 0) {
              c.target = queueSpots[i % 2];
              c.state = WALKING_TO_MACHINE;
            } else {
              c.target = glm::vec3(0.0f, 0.0f, -5.0f);
              c.state = WALKING_TO_JUNCTION;
            }
          } else {
            c.target = glm::vec3(0.0f, 0.0f, 25.0f);
            c.state = WALKING_OUT;
          }
        } else if (c.state == WALKING_TO_MACHINE) {
          c.state = WAITING_FOR_COFFEE;
          c.timer = 5.0f;
          if (robotState == IDLE) {
            robotState = MOVE_TO_PICKUP;
            targetNPCIdx = i;
            stateTimer = 0.0f;
          }
        } else if (c.state == WALKING_TO_JUNCTION) {
          c.target = allSeats[c.assignedSeat].pos;
          c.state = WALKING_TO_SEAT;
        } else if (c.state == WALKING_TO_SEAT) {
          c.pos = allSeats[c.assignedSeat].pos;
          c.state = SITTING;
          c.timer = 15.0f + (rand() % 20);
        } else if (c.state == WALKING_OUT_JUNCTION) {
          c.target = glm::vec3(0.0f, 0.0f, 25.0f);
          c.state = WALKING_OUT;
        } else if (c.state == WALKING_OUT) {
          c.active = false;
          c.hasReceivedCoffee = false;
        }
      } else {
        c.pos += glm::normalize(dir) * c.speed * dt;
      }
    } else if (c.state == WAITING_FOR_COFFEE) {
      c.timer -= dt; // Will be reset by robot when served
      if (c.timer <= 0) {
        c.target = glm::vec3(0.0f, 0.0f, -5.0f);
        c.state = WALKING_TO_JUNCTION;
      }
    } else if (c.state == SITTING) {
      c.timer -= dt;
      if (c.timer <= 0) {
        allSeats[c.assignedSeat].occupiedBy = 0;
        c.assignedSeat = -1;
        c.target = glm::vec3(0.0f, 0.0f, -5.0f);
        c.state = WALKING_OUT_JUNCTION;
      }
    }
  }
}

void drawNPCs(Shader &ourShader, unsigned int vao, Cylinder &cylinder) {
  float t = (float)glfwGetTime();
  bool anyNPCNearDoor = false;

  for (int i = 0; i < MAX_CUSTOMERS; ++i) {
    if (!customers[i].active)
      continue;
    Customer &c = customers[i];
    float cDx = abs(c.pos.x - 0.0f);
    float cDz = c.pos.z;
    if (cDx < 1.8f && cDz > 0.5f && cDz < 3.0f)
      anyNPCNearDoor = true;

    float bodyH = 1.1f;
    float bodyY = 1.6f;
    glm::vec3 seatedOffset(0.0f);

    if (c.state == SITTING && c.assignedSeat >= 0) {
      Seat &s = allSeats[c.assignedSeat];
      // Pelvis height = seat surface (approx 0.995) + small pelvis offset
      bodyY = 1.32f;

      // Backward inset: move pelvis toward the chair back based on facing angle
      float rad = glm::radians(s.facingAngle);
      // Inset distance approx 0.35 units backward
      seatedOffset = glm::vec3(cos(rad) * -0.35f, 0.0f, sin(rad) * -0.35f);
    }

    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, c.pos + glm::vec3(0, bodyY, 0) + seatedOffset);

    // Orientation Phase
    if (c.state == SITTING && c.assignedSeat >= 0) {
      m = customRotate(
          m, glm::radians(90.0f - allSeats[c.assignedSeat].facingAngle),
          glm::vec3(0, 1, 0));
    } else {
      glm::vec3 dir = c.target - c.pos;
      if (glm::length(dir) > 0.05f) {
        float angle = glm::degrees(std::atan2(dir.x, dir.z));
        m = customRotate(m, glm::radians(angle), glm::vec3(0, 1, 0));
      }
    }

    // Animation Phase
    float swing = 0.0f;
    if (c.state != SITTING && c.state != WAITING_FOR_COFFEE &&
        glm::length(c.target - c.pos) > 0.05f) {
      swing = sin(t * 8.0f + i * 1.5f) * 35.0f;
    }

    ourShader.setBool("uUseTexture", false);
    glBindVertexArray(vao);

    // --- Randomized clothing palette ---
    int seed = c.clothingSeed;
    float skinR = 0.72f + (seed % 7) * 0.03f;
    float skinG = 0.55f + (seed % 5) * 0.03f;
    float skinB = 0.40f + (seed % 4) * 0.03f;
    glm::vec4 skinColor = glm::vec4(skinR, skinG, skinB, 1.0f);

    float shR = ((seed * 7) % 100) / 200.0f + 0.15f;
    float shG = ((seed * 13) % 100) / 200.0f + 0.15f;
    float shB = ((seed * 23) % 100) / 200.0f + 0.20f;
    glm::vec4 shirtColor = glm::vec4(shR, shG, shB, 1.0f);

    float pR = 0.12f + ((seed * 3) % 20) * 0.01f;
    float pG = 0.10f + ((seed * 5) % 15) * 0.01f;
    float pB = 0.15f + ((seed * 11) % 20) * 0.01f;
    glm::vec4 pantsColor = glm::vec4(pR, pG, pB, 1.0f);

    float bR = 0.08f + ((seed * 17) % 15) * 0.01f;
    glm::vec4 bootColor = glm::vec4(bR, bR * 0.8f, bR * 0.6f, 1.0f);

    bool hasVest = (seed % 3 != 0);
    bool hasHat = (seed % 4 != 0);
    bool hasJacket = (seed % 5 == 0);

    float vR = 0.35f + ((seed * 19) % 25) * 0.01f;
    float vG = 0.20f + ((seed * 7) % 20) * 0.01f;
    float vB = 0.08f + ((seed * 31) % 15) * 0.01f;
    glm::vec4 vestColor = glm::vec4(vR, vG, vB, 1.0f);

    float hR = 0.30f + ((seed * 29) % 30) * 0.01f;
    float hG = 0.20f + ((seed * 37) % 20) * 0.01f;
    float hB = 0.08f + ((seed * 41) % 15) * 0.01f;
    glm::vec4 hatColor = glm::vec4(hR, hG, hB, 1.0f);

    // --- TORSO (RATIO-BASED) ---
    // Upper chest
    glm::mat4 chestM = glm::translate(m, glm::vec3(0.0f, bodyH * 0.12f, 0.0f));
    chestM = glm::scale(chestM, glm::vec3(0.35f, bodyH * 0.40f, 0.20f));
    ourShader.setMat4("model", chestM);
    ourShader.setV4("baseColor", shirtColor);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    // Waist
    glm::mat4 waistM = glm::translate(m, glm::vec3(0.0f, -bodyH * 0.15f, 0.0f));
    waistM = glm::scale(waistM, glm::vec3(0.28f, bodyH * 0.22f, 0.18f));
    ourShader.setMat4("model", waistM);
    ourShader.setV4("baseColor", shirtColor * 0.9f);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    // Outfits
    if (hasVest) {
      glm::mat4 vestM = glm::translate(m, glm::vec3(0.0f, bodyH * 0.12f, 0.0f));
      vestM = glm::scale(vestM, glm::vec3(0.37f, bodyH * 0.38f, 0.21f));
      ourShader.setMat4("model", vestM);
      ourShader.setV4("baseColor", vestColor);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    if (hasJacket) {
      glm::mat4 jkM = glm::translate(m, glm::vec3(0.0f, bodyH * 0.05f, 0.0f));
      jkM = glm::scale(jkM, glm::vec3(0.38f, bodyH * 0.52f, 0.22f));
      ourShader.setMat4("model", jkM);
      ourShader.setV4("baseColor", vestColor * 0.8f);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    // --- SHOULDERS & NECK ---
    for (int side = -1; side <= 1; side += 2) {
      glm::mat4 shM =
          glm::translate(m, glm::vec3(side * 0.20f, bodyH * 0.28f, 0.0f));
      shM = glm::scale(shM, glm::vec3(0.12f, 0.10f, 0.15f));
      ourShader.setMat4("model", shM);
      ourShader.setV4("baseColor", shirtColor);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glm::mat4 neckM = glm::translate(m, glm::vec3(0.0f, bodyH * 0.35f, 0.0f));
    neckM = glm::scale(neckM, glm::vec3(0.09f, 0.12f, 0.09f));
    ourShader.setMat4("model", neckM);
    ourShader.setV4("baseColor", skinColor);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    glm::mat4 headM = glm::translate(m, glm::vec3(0.0f, bodyH * 0.50f, 0.0f));
    headM = glm::scale(headM, glm::vec3(0.25f, 0.28f, 0.25f));

    ourShader.setMat4("model", headM);
    ourShader.setV4("baseColor", skinColor);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    if (hasHat) {
      glm::mat4 brimM = glm::translate(m, glm::vec3(0.0f, bodyH * 0.62f, 0.0f));
      brimM = glm::scale(brimM, glm::vec3(0.45f, 0.04f, 0.45f));
      ourShader.setMat4("model", brimM);
      ourShader.setV4("baseColor", hatColor);
      glDrawArrays(GL_TRIANGLES, 0, 36);
      glm::mat4 crownM =
          glm::translate(m, glm::vec3(0.0f, bodyH * 0.68f, 0.0f));
      crownM = glm::scale(crownM, glm::vec3(0.20f, 0.18f, 0.20f));
      ourShader.setMat4("model", crownM);
      ourShader.setV4("baseColor", hatColor * 1.1f);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    // --- SEGMENTED LIMBS ---
    glm::mat4 rightHandMat = glm::mat4(1.0f);

    auto drawHumanLimb = [&](glm::vec3 off, float a, bool leg, int side) {
      glm::mat4 up = glm::translate(m, off);
      up = customRotate(up, glm::radians(a), glm::vec3(1, 0, 0));

      float upperLen = leg ? bodyH * 0.45f : bodyH * 0.32f;
      float lowerLen = leg ? bodyH * 0.40f : bodyH * 0.28f;

      // Upper segment
      glm::mat4 upDraw = glm::translate(up, glm::vec3(0, -upperLen * 0.4f, 0));
      upDraw =
          glm::scale(upDraw, leg ? glm::vec3(0.14f, upperLen * 0.8f, 0.14f)
                                 : glm::vec3(0.10f, upperLen * 0.8f, 0.10f));
      ourShader.setMat4("model", upDraw);
      ourShader.setV4("baseColor", leg ? pantsColor : skinColor);
      glDrawArrays(GL_TRIANGLES, 0, 36);

      // Lower segment
      glm::mat4 lo = glm::translate(up, glm::vec3(0, -upperLen * 0.8f, 0));
      if (c.state == SITTING) {
        // Bend knees forward then down
        lo = customRotate(lo, glm::radians(leg ? 90.0f : -45.0f),
                          glm::vec3(1, 0, 0));
      }
      glm::mat4 loDraw = glm::translate(lo, glm::vec3(0, -lowerLen * 0.4f, 0));
      loDraw =
          glm::scale(loDraw, leg ? glm::vec3(0.13f, lowerLen * 0.8f, 0.13f)
                                 : glm::vec3(0.09f, lowerLen * 0.8f, 0.09f));
      ourShader.setMat4("model", loDraw);
      ourShader.setV4("baseColor", leg ? pantsColor : skinColor);
      glDrawArrays(GL_TRIANGLES, 0, 36);

      if (leg) {
        glm::mat4 bootM =
            glm::translate(lo, glm::vec3(0, -lowerLen * 0.8f, 0.05f));
        bootM = glm::scale(bootM, glm::vec3(0.15f, 0.12f, 0.20f));
        ourShader.setMat4("model", bootM);
        ourShader.setV4("baseColor", bootColor);
        glDrawArrays(GL_TRIANGLES, 0, 36);
      } else {
        glm::mat4 handM = glm::translate(lo, glm::vec3(0, -lowerLen * 0.8f, 0));
        if (side == 1)
          rightHandMat = handM; // Capture for mug
        handM = glm::scale(handM, glm::vec3(0.08f, 0.08f, 0.06f));
        ourShader.setMat4("model", handM);
        ourShader.setV4("baseColor", skinColor);
        glDrawArrays(GL_TRIANGLES, 0, 36);
      }
    };

    // Arms
    drawHumanLimb(glm::vec3(-0.25f, bodyH * 0.25f, 0),
                  -swing + (c.state == SITTING ? 25.0f : 0.0f), false, -1);
    drawHumanLimb(glm::vec3(0.25f, bodyH * 0.25f, 0),
                  swing + (c.state == SITTING ? 30.0f : 0.0f), false, 1);

    // Legs
    drawHumanLimb(glm::vec3(-0.12f, -bodyH * 0.25f, 0),
                  (c.state == SITTING ? -90.0f : swing), true, -1);
    drawHumanLimb(glm::vec3(0.12f, -bodyH * 0.25f, 0),
                  (c.state == SITTING ? -90.0f : -swing), true, 1);

    // --- COFFEE MUG Parented to Hand ---
    if (c.hasReceivedCoffee) {
      // Use the captured Hand matrix
      glm::mat4 mugAnchor =
          glm::translate(rightHandMat, glm::vec3(0.0f, -0.05f, 0.08f));
      if (c.state == SITTING) {
        mugAnchor =
            glm::translate(rightHandMat, glm::vec3(0.0f, -0.05f, 0.12f));
      }

      glm::mat4 mugModel =
          glm::scale(mugAnchor, glm::vec3(0.12f, 0.15f, 0.12f));
      ourShader.setMat4("model", mugModel);
      ourShader.setV4("baseColor", glm::vec4(0.92f, 0.88f, 0.82f, 1.0f));
      applyTexModeToShader(ourShader);
      if (gTexMode != TEX_OFF) bindTex0(ourShader, waterTexture, 0);
      glDrawArrays(GL_TRIANGLES, 0, 36);

      // Liquid inside
      glm::mat4 coffeeModel =
          glm::translate(mugAnchor, glm::vec3(0.0f, 0.06f, 0.0f));
      coffeeModel = glm::scale(coffeeModel, glm::vec3(0.09f, 0.02f, 0.09f));
      ourShader.setMat4("model", coffeeModel);
      ourShader.setV4("baseColor", glm::vec4(0.25f, 0.15f, 0.05f, 1.0f));
      ourShader.setBool("uUseTexture", false);
      glDrawArrays(GL_TRIANGLES, 0, 36);

    }
  }

  // Determine door swing direction based on the person who opened it
  // Fix: Only recalculate direction when door is nearly closed to prevent mid-swing hitching
  if (isDoorOpen && std::abs(doorAngle) < 5.0f) {

    if (anyNPCNearDoor) {
      // Find which NPC is nearest and check their state
      for (int i = 0; i < MAX_CUSTOMERS; ++i) {
        if (!customers[i].active) continue;
        float dist = horizontalDistanceXZ(customers[i].pos, glm::vec3(0.0f, 0.0f, 1.8f));
        if (dist < 3.0f) {
           // Entering (at z > 1.8 moving to machine) -> Inward (-1), else Outward (1)
           doorSwingDirection = (customers[i].pos.z > 1.8f) ? -1.0f : 1.0f;
           break;
        }
      }
    } else if (playerWantsDoorOpen) {
      doorSwingDirection = (camera.Position.z > 1.8f) ? -1.0f : 1.0f;
    }
  }

  isDoorOpen = anyNPCNearDoor || playerWantsDoorOpen;

}

bool diffuseOn = true;
bool specularOn = true;

glm::vec3 canopyPointLights[] = {
    glm::vec3(-5.0f, 6.0f, -5.0f), glm::vec3(5.0f, 6.0f, -5.0f),
    glm::vec3(-5.0f, 6.0f, 5.0f), glm::vec3(5.0f, 6.0f, 5.0f)};

// Cube Vertices (pos, normal, uv)

float cubeVertices[] = {
    // positions          // normals           // tex coords
    -0.5f, -0.5f, -0.5f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0.5f,  -0.5f,
    -0.5f, 0.0f,  0.0f,  -1.0f, 1.0f,  0.0f,  0.5f,  0.5f,  -0.5f, 0.0f,
    0.0f,  -1.0f, 1.0f,  1.0f,  0.5f,  0.5f,  -0.5f, 0.0f,  0.0f,  -1.0f,
    1.0f,  1.0f,  -0.5f, 0.5f,  -0.5f, 0.0f,  0.0f,  -1.0f, 0.0f,  1.0f,
    -0.5f, -0.5f, -0.5f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,

    -0.5f, -0.5f, 0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  0.5f,  -0.5f,
    0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  0.0f,  0.5f,  0.5f,  0.5f,  0.0f,
    0.0f,  1.0f,  1.0f,  1.0f,  0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
    1.0f,  1.0f,  -0.5f, 0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  1.0f,
    -0.5f, -0.5f, 0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,

    -0.5f, 0.5f,  0.5f,  -1.0f, 0.0f,  0.0f,  1.0f,  0.0f,  -0.5f, 0.5f,
    -0.5f, -1.0f, 0.0f,  0.0f,  1.0f,  1.0f,  -0.5f, -0.5f, -0.5f, -1.0f,
    0.0f,  0.0f,  0.0f,  1.0f,  -0.5f, -0.5f, -0.5f, -1.0f, 0.0f,  0.0f,
    0.0f,  1.0f,  -0.5f, -0.5f, 0.5f,  -1.0f, 0.0f,  0.0f,  0.0f,  0.0f,
    -0.5f, 0.5f,  0.5f,  -1.0f, 0.0f,  0.0f,  1.0f,  0.0f,

    0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.5f,  0.5f,
    -0.5f, 1.0f,  0.0f,  0.0f,  1.0f,  1.0f,  0.5f,  -0.5f, -0.5f, 1.0f,
    0.0f,  0.0f,  0.0f,  1.0f,  0.5f,  -0.5f, -0.5f, 1.0f,  0.0f,  0.0f,
    0.0f,  1.0f,  0.5f,  -0.5f, 0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
    0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

    -0.5f, -0.5f, -0.5f, 0.0f,  -1.0f, 0.0f,  0.0f,  1.0f,  0.5f,  -0.5f,
    -0.5f, 0.0f,  -1.0f, 0.0f,  1.0f,  1.0f,  0.5f,  -0.5f, 0.5f,  0.0f,
    -1.0f, 0.0f,  1.0f,  0.0f,  0.5f,  -0.5f, 0.5f,  0.0f,  -1.0f, 0.0f,
    1.0f,  0.0f,  -0.5f, -0.5f, 0.5f,  0.0f,  -1.0f, 0.0f,  0.0f,  0.0f,
    -0.5f, -0.5f, -0.5f, 0.0f,  -1.0f, 0.0f,  0.0f,  1.0f,

    -0.5f, 0.5f,  -0.5f, 0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.5f,  0.5f,
    -0.5f, 0.0f,  1.0f,  0.0f,  1.0f,  1.0f,  0.5f,  0.5f,  0.5f,  0.0f,
    1.0f,  0.0f,  1.0f,  0.0f,  0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
    1.0f,  0.0f,  -0.5f, 0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  0.0f,
    -0.5f, 0.5f,  -0.5f, 0.0f,  1.0f,  0.0f,  0.0f,  1.0f};

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
        v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y});
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
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(),
                 GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(6 * sizeof(float)));
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
void drawCube(Shader &shader, unsigned int vao, glm::vec3 pos, glm::vec3 scale,
              glm::vec4 color, unsigned int texID = 0) {
  shader.setV4("baseColor", color);

  applyTexModeToShader(shader);

  if (texID != 0 && gTexMode != TEX_OFF) {
    bindTex0(shader, texID, 0);
  } else {
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

void drawMug(Shader &shader, Cylinder &cylinder, glm::vec3 pos, float radius,
             float height, glm::vec4 color, unsigned int texID = 0) {
  // Body - height is along the cylinder's local Z axis (before rotation)
  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, pos);
  model = glm::rotate(model, glm::radians(90.0f),
                      glm::vec3(1, 0, 0)); // Z-axis to Y-axis
  model = glm::scale(model, glm::vec3(radius, radius, height));
  shader.setMat4("model", model);
  shader.setV4("baseColor", color);

  applyTexModeToShader(shader);
  if (texID != 0 && gTexMode != TEX_OFF)
    bindTex0(shader, texID, 0);
  else
    shader.setBool("uUseTexture", false);

  cylinder.draw();

  // Handle (small vertical cylinder segment on the side)
  model = glm::mat4(1.0f);
  model = glm::translate(model, pos + glm::vec3(radius * 0.9f, 0, 0));
  model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
  model = glm::scale(model,
                     glm::vec3(radius * 0.25f, radius * 0.25f, height * 0.7f));
  shader.setMat4("model", model);
  shader.setV4("baseColor", color * 0.8f);
  shader.setBool("uUseTexture", false);
  cylinder.draw();
}

void drawCup(Shader &shader, Cylinder &cylinder, glm::vec3 pos, float radius,
             float height, glm::vec4 color, unsigned int texID = 0) {
  // Body
  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, pos);
  model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
  model = glm::scale(model, glm::vec3(radius, radius, height));
  shader.setMat4("model", model);
  shader.setV4("baseColor", color);

  applyTexModeToShader(shader);
  if (texID != 0 && gTexMode != TEX_OFF)
    bindTex0(shader, texID, 0);
  else
    shader.setBool("uUseTexture", false);

  cylinder.draw();
}

// Outline helper
void drawCubeWithOutline(Shader &shader, unsigned int vao, glm::vec3 pos,
                         glm::vec3 scale, glm::vec4 color,
                         float outlineThickness = 0.05f) {
  glm::vec4 outlineColor = glm::vec4(0.05f, 0.05f, 0.07f, 1.0f);
  drawCube(shader, vao, pos, scale + glm::vec3(outlineThickness), outlineColor,
           0);
  drawCube(shader, vao, pos, scale, color, 0);
}

// Stylized Table Set (textured)
void drawStylizedTableSet(Shader &shader, Sphere &sphere, Cylinder &cylinder,
                          unsigned int vao, glm::vec3 offset, float zDist, int tableIdx) {

  float depthScale = 1.0f - glm::clamp((zDist + 5.0f) / 100.0f, 0.0f, 0.15f);

  glm::vec4 tableColor = glm::vec4(0.70f, 0.48f, 0.25f, 1.0f);
  glm::vec4 chairColor = glm::vec4(0.20f, 0.14f, 0.10f, 1.0f);

  float rot = 5.0f * sin(offset.x * 0.5f + offset.z * 0.3f);
  float vH = 0.2f;

  auto drawPart = [&](glm::vec3 pos, glm::vec3 scale, glm::vec4 color,
                      unsigned int texID) {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, offset);
    model = glm::rotate(model, glm::radians(rot), glm::vec3(0, 1, 0));
    model = glm::translate(model, pos + glm::vec3(0, vH, 0));
    model = glm::scale(model, scale * depthScale);

    shader.setMat4("model", model);
    shader.setV4("baseColor", color);

    applyTexModeToShader(shader);
    if (texID != 0 && gTexMode != TEX_OFF)
      bindTex0(shader, texID, 0);
    else
      shader.setBool("uUseTexture", false);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
  };

  // Table (wood texture)
  drawPart(glm::vec3(0, 0.62f, 0), glm::vec3(2.4f, 0.12f, 1.4f), tableColor,
           woodTexture);
  drawPart(glm::vec3(0, 0.58f, 0), glm::vec3(2.42f, 0.08f, 1.42f),
           tableColor * 0.88f, woodTexture);

  // Phase 1: Procedural Custom Cafe Objects (Bezier Surface)
  auto drawBezierGlass = [&](glm::vec3 pos, glm::vec3 s, glm::vec4 color, unsigned int texID = 0) {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, offset);
    model = customRotate(model, glm::radians(rot),
                         glm::vec3(0, 1, 0)); // Custom Math!
    model = glm::translate(model, pos + glm::vec3(0, vH, 0));
    model = glm::scale(model, s * depthScale);

    shader.setMat4("model", model);
    shader.setV4("baseColor", color);

    applyTexModeToShader(shader);
    if (texID != 0 && gTexMode != TEX_OFF) {
       bindTex0(shader, texID, 0);
    } else {
       shader.setBool("uUseTexture", false);
    }

    glBindVertexArray(coffeeVAO);
    glDrawElements(GL_TRIANGLES, coffeeIndexCount, GL_UNSIGNED_INT, 0);
  };


  // Exactly 4 Coffee Glasses (Bezier Surface) with occupancy awareness
  glm::vec3 glassSpots[] = {
      glm::vec3(-0.45f, 0.68f, -0.3f), glm::vec3(0.45f, 0.68f, -0.3f),
      glm::vec3(-0.45f, 0.68f, 0.3f), glm::vec3(0.45f, 0.68f, 0.3f)};
  for (int i = 0; i < 4; ++i) {
    // A mug is hidden if the seat facing it is occupied and the person has "taken" it
    bool mugTaken = false;
    int seatIdx = tableIdx * 4 + i;
    if (seatIdx < (int)allSeats.size() && allSeats[seatIdx].occupiedBy != 0) {
        // Seat 0-3 correspond to glassSpots 0-3
        // If seat is occupied, we assume the person takes the mug
        mugTaken = true; 
    }

    if (!mugTaken) {
      drawBezierGlass(glassSpots[i], glm::vec3(0.5f),
                    glm::vec4(0.95f, 0.95f, 0.98f, 1.0f), waterTexture);
    }
  }


  // Central Sugar Canister (Cylinder)
  glm::mat4 canM = glm::mat4(1.0f);
  canM = glm::translate(canM, offset);
  canM = customRotate(canM, glm::radians(rot), glm::vec3(0, 1, 0));
  canM = glm::translate(canM, glm::vec3(0.0f, 0.68f + vH + 0.05f, 0.0f));
  canM = glm::scale(canM, glm::vec3(0.12f, 0.2f, 0.12f) * depthScale);
  shader.setMat4("model", canM);
  shader.setV4("baseColor", glm::vec4(0.85f, 0.85f, 0.88f, 1.0f)); // Silver tin
  cylinder.draw();

  // Legs
  drawPart(glm::vec3(-0.9f, 0.3f, -0.5f), glm::vec3(0.12f, 0.8f, 0.12f),
           tableColor * 0.9f, woodTexture);
  drawPart(glm::vec3(0.9f, 0.3f, -0.5f), glm::vec3(0.12f, 0.8f, 0.12f),
           tableColor * 0.9f, woodTexture);
  drawPart(glm::vec3(-0.9f, 0.3f, 0.5f), glm::vec3(0.12f, 0.8f, 0.12f),
           tableColor * 0.9f, woodTexture);
  drawPart(glm::vec3(0.9f, 0.3f, 0.5f), glm::vec3(0.12f, 0.8f, 0.12f),
           tableColor * 0.9f, woodTexture);

  // Chairs (wood texture too)
  glm::vec3 chairPos[] = {
      glm::vec3(0.0f, 0.0f, 1.6f), glm::vec3(0.0f, 0.0f, -1.6f),
      glm::vec3(1.7f, 0.0f, 0.0f), glm::vec3(-1.7f, 0.0f, 0.0f)};
  float chairRots[] = {0.0f, 180.0f, 90.0f, -90.0f};

  for (int i = 0; i < 4; ++i) {
    float chairRot = chairRots[i];

    auto drawChairPart = [&](glm::vec3 p, glm::vec3 s, glm::vec4 c,
                             unsigned int texID) {
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
      if (texID != 0 && gTexMode != TEX_OFF)
        bindTex0(shader, texID, 0);
      else
        shader.setBool("uUseTexture", false);

      glBindVertexArray(vao);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    };

    drawChairPart(glm::vec3(0, 0.42f, 0), glm::vec3(1.1f, 0.15f, 1.1f),
                  chairColor, woodTexture);
    drawChairPart(glm::vec3(0, 1.0f, 0.5f), glm::vec3(1.1f, 1.0f, 0.1f),
                  chairColor, woodTexture);

    drawChairPart(glm::vec3(-0.4f, 0.2f, -0.4f), glm::vec3(0.15f, 0.4f, 0.15f),
                  chairColor * 0.85f, woodTexture);
    drawChairPart(glm::vec3(0.4f, 0.2f, -0.4f), glm::vec3(0.15f, 0.4f, 0.15f),
                  chairColor * 0.85f, woodTexture);
    drawChairPart(glm::vec3(-0.4f, 0.2f, 0.4f), glm::vec3(0.15f, 0.4f, 0.15f),
                  chairColor * 0.85f, woodTexture);
    drawChairPart(glm::vec3(0.4f, 0.2f, 0.4f), glm::vec3(0.15f, 0.4f, 0.15f),
                  chairColor * 0.85f, woodTexture);
  }
}

// ================= PHASE 4 OBJECTS =================

void drawFan(Shader &shader, Cylinder &cylinder, unsigned int cubeVAO,
             glm::vec3 pos) {
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
    bModel = customRotate(bModel, glm::radians(fanAngle + i * 90.0f),
                          glm::vec3(0, 1, 0)); // Custom math!
    bModel = glm::translate(bModel, glm::vec3(1.2f, 0, 0));
    bModel = glm::scale(bModel, glm::vec3(2.0f, 0.05f, 0.3f));
    shader.setMat4("model", bModel);
    shader.setV4("baseColor", glm::vec4(0.3f, 0.2f, 0.15f, 1.0f));

    applyTexModeToShader(shader);
    if (gTexMode != TEX_OFF)
      bindTex0(shader, woodTexture, 0);
    else
      shader.setBool("uUseTexture", false);

    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
  }
}

void drawDoor(Shader &shader, unsigned int cubeVAO, glm::vec3 hingePos) {
  float doorWidth = 3.0f;
  float doorHeight = 4.8f;
  float doorThickness = 0.06f;

  // Hinge Transformation
  glm::mat4 hinge = glm::mat4(1.0f);
  hinge = glm::translate(hinge, hingePos);
  hinge = customRotate(hinge, glm::radians(doorAngle), glm::vec3(0, 1, 0));

  applyTexModeToShader(shader);
  shader.setBool("uUseTexture", false);
  glBindVertexArray(cubeVAO);

  // 1. Door Frame (dark border - drawn first, behind panel)
  glm::vec4 frameCol = glm::vec4(0.15f, 0.12f, 0.10f, 1.0f);
  glm::mat4 doorBody =
      glm::translate(hinge, glm::vec3(doorWidth / 2.0f, doorHeight / 2.0f, 0));
  glm::mat4 model =
      glm::scale(doorBody, glm::vec3(doorWidth + 0.15f, doorHeight + 0.15f,
                                     doorThickness));
  shader.setMat4("model", model);
  shader.setV4("baseColor", frameCol);
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // 2. Door Panel (clean warm wood - offset forward to avoid z-fighting)
  glm::mat4 panelBody = glm::translate(doorBody, glm::vec3(0.0f, 0.0f, 0.02f));
  model = glm::scale(
      panelBody, glm::vec3(doorWidth - 0.1f, doorHeight - 0.1f, doorThickness));
  shader.setMat4("model", model);
  shader.setV4("baseColor", glm::vec4(0.55f, 0.38f, 0.22f, 1.0f));
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // 3. Glass Insert (upper half - further forward)
  glm::mat4 glassM =
      glm::translate(panelBody, glm::vec3(0.0f, doorHeight * 0.15f, 0.02f));
  glassM = glm::scale(glassM,
                      glm::vec3(doorWidth * 0.70f, doorHeight * 0.40f, 0.02f));
  shader.setMat4("model", glassM);
  shader.setV4("baseColor", glm::vec4(0.55f, 0.72f, 0.88f, 0.35f));
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // 4. Door Handle (small bar on the right side)
  glm::mat4 handleM =
      glm::translate(panelBody, glm::vec3(doorWidth * 0.35f, -0.2f, 0.04f));
  handleM = glm::scale(handleM, glm::vec3(0.06f, 0.35f, 0.06f));
  shader.setMat4("model", handleM);
  shader.setV4("baseColor", glm::vec4(0.72f, 0.70f, 0.68f, 1.0f));
  glDrawArrays(GL_TRIANGLES, 0, 36);
}

// ================= CAFE SIGN (3D Block Letters) =================
void drawBlockLetter(Shader &shader, unsigned int cubeVAO, glm::mat4 base,
                     char ch, float size) {
  // Each letter is a set of small cubes forming a simplified block letter
  float s = size;
  float t = s * 0.18f; // thickness of strokes

  auto bar = [&](float x, float y, float w, float h) {
    glm::mat4 bm = glm::translate(base, glm::vec3(x * s, y * s, 0.0f));
    bm = glm::scale(bm, glm::vec3(w * s, h * s, t));
    shader.setMat4("model", bm);
    glDrawArrays(GL_TRIANGLES, 0, 36);
  };

  switch (ch) {
  case 'C':
    bar(-0.3f, 0, t, 1.0f);
    bar(0, 0.45f, 0.6f, t);
    bar(0, -0.45f, 0.6f, t);
    break;
  case 'A':
    bar(-0.3f, 0, t, 1.0f);
    bar(0.3f, 0, t, 1.0f);
    bar(0, 0.45f, 0.6f, t);
    bar(0, 0, 0.6f, t);
    break;
  case 'F':
    bar(-0.3f, 0, t, 1.0f);
    bar(0, 0.45f, 0.6f, t);
    bar(-0.05f, 0, 0.5f, t);
    break;
  case 'E':
    bar(-0.3f, 0, t, 1.0f);
    bar(0, 0.45f, 0.6f, t);
    bar(-0.05f, 0, 0.5f, t);
    bar(0, -0.45f, 0.6f, t);
    break;
  case 'B':
    bar(-0.3f, 0, t, 1.0f);
    bar(0, 0.45f, 0.5f, t);
    bar(0, 0, 0.5f, t);
    bar(0, -0.45f, 0.5f, t);
    bar(0.25f, 0.22f, t, 0.5f);
    bar(0.25f, -0.22f, t, 0.5f);
    break;
  case 'L':
    bar(-0.3f, 0, t, 1.0f);
    bar(0, -0.45f, 0.6f, t);
    break;
  case 'H':
    bar(-0.3f, 0, t, 1.0f);
    bar(0.3f, 0, t, 1.0f);
    bar(0, 0, 0.6f, t);
    break;
  case 'R':
    bar(-0.3f, 0, t, 1.0f);
    bar(0, 0.45f, 0.5f, t);
    bar(0, 0, 0.5f, t);
    bar(0.25f, 0.22f, t, 0.5f);
    bar(0.1f, -0.25f, t, 0.45f);
    break;
  case 'I':
    bar(0, 0, t, 1.0f);
    bar(0, 0.45f, 0.3f, t);
    bar(0, -0.45f, 0.3f, t);
    break;
  case 'N':
    bar(-0.3f, 0, t, 1.0f);
    bar(0.3f, 0, t, 1.0f);
    bar(0, 0, t * 0.7f, 1.0f);
    break;
  case ' ':
    break; // space
  default:
    bar(0, 0, 0.4f, 0.8f);
    break; // fallback block
  }
}

void drawCafeSign(Shader &shader, unsigned int cubeVAO) {
  const char *text = "CAFE BEEL HARINA";
  int len = 16;
  float letterSize = 0.28f;
  float spacing = letterSize * 0.85f;
  float totalWidth = len * spacing;
  float startX = -totalWidth / 2.0f;

  // Sign position: above the door
  glm::vec3 signPos = glm::vec3(0.0f, 5.8f, 1.9f);

  shader.setBool("uUseTexture", false);
  glBindVertexArray(cubeVAO);

  // Signboard backing
  glm::mat4 backM = glm::mat4(1.0f);
  backM = glm::translate(backM, signPos + glm::vec3(0.0f, 0.0f, -0.04f));
  backM = glm::scale(backM, glm::vec3(totalWidth + 0.6f, 1.0f, 0.08f));
  shader.setMat4("model", backM);
  shader.setV4("baseColor", glm::vec4(0.10f, 0.08f, 0.06f, 1.0f));
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // Border trim
  glm::mat4 trimM = glm::mat4(1.0f);
  trimM = glm::translate(trimM, signPos + glm::vec3(0.0f, 0.0f, -0.03f));
  trimM = glm::scale(trimM, glm::vec3(totalWidth + 0.7f, 1.1f, 0.06f));
  shader.setMat4("model", trimM);
  shader.setV4("baseColor", glm::vec4(0.55f, 0.38f, 0.18f, 1.0f));
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // Letter rendering
  if (signLightOn) {
    shader.setBool("uIsEmissiveObject", true);
    shader.setV4("baseColor", glm::vec4(1.0f, 0.92f, 0.65f, 1.0f));
  } else {
    shader.setV4("baseColor", glm::vec4(0.75f, 0.65f, 0.45f, 1.0f));
  }

  for (int i = 0; i < len; ++i) {
    glm::mat4 letterBase = glm::mat4(1.0f);
    letterBase = glm::translate(
        letterBase,
        signPos + glm::vec3(startX + i * spacing + spacing * 0.5f, 0.0f, 0.0f));
    drawBlockLetter(shader, cubeVAO, letterBase, text[i], letterSize);
  }

  if (signLightOn) {
    shader.setBool("uIsEmissiveObject", false);
  }
}

void drawClock(Shader &shader, Cylinder &cylinder, unsigned int cubeVAO,
               glm::vec3 pos) {
  time_t t = time(0);
  struct tm *now = localtime(&t);

  float seconds = static_cast<float>(now->tm_sec);
  float minutes = now->tm_min + seconds / 60.0f; // fluid minute progression
  float hours = (now->tm_hour % 12) + minutes / 60.0f; // fluid hour progression

  float secAngle = -seconds * 6.0f;
  float minAngle = -minutes * 6.0f;
  float hrAngle = -hours * 30.0f;

  // 1. Structural Backing Plate (Square, White, for visibility)
  glm::mat4 plate = glm::mat4(1.0f);
  plate = glm::translate(plate, pos + glm::vec3(0, 0, -0.04f));
  plate = glm::scale(plate, glm::vec3(1.5f, 1.5f, 0.02f));
  shader.setMat4("model", plate);
  shader.setV4("baseColor", glm::vec4(0.05f, 0.05f, 0.05f, 1.0f));

  shader.setBool("uUseTexture", false);
  glBindVertexArray(cubeVAO);
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // 2. Main Circular Face (Black)
  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, pos);
  model = glm::scale(model, glm::vec3(1.2f, 1.2f, 0.04f));
  shader.setMat4("model", model);
  shader.setV4("baseColor", glm::vec4(0.05f, 0.05f, 0.05f, 1.0f));
  applyTexModeToShader(shader);
  shader.setBool("uUseTexture", false);
  cylinder.draw();

  // 3. Metallic Rim
  glm::mat4 rim = glm::mat4(1.0f);
  rim = glm::translate(rim, pos);
  rim = glm::scale(rim, glm::vec3(1.3f, 1.3f, 0.02f));
  shader.setMat4("model", rim);
  shader.setV4("baseColor", glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));
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
  drawHand(hrAngle, 0.08f, 0.6f,
           glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)); // White Hour
  drawHand(minAngle, 0.05f, 0.9f,
           glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)); // White Min

  drawHand(secAngle, 0.02f, 1.0f,
           glm::vec4(1.0f, 0.2f, 0.2f, 1.0f)); // Bright Red Sec
}

void drawCoffeeMachine(Shader &shader, unsigned int cubeVAO, Cylinder &cylinder,
                       glm::vec3 pos) {
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
  lModel = customRotate(lModel, glm::radians(brewAngle),
                        glm::vec3(1, 0, 0)); // Custom math lever!
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

// ================= HIERARCHICAL ROBOTIC ARM =================
void drawRoboticArm(Shader &shader, Cylinder &cylinder, unsigned int cubeVAO,
                    glm::vec3 basePos) {
  // 6-Phase Keyframe Logic
  float baseRot = 0.0f, shoulderAngle = -15.0f, elbowAngle = -10.0f,
        wristAngle = 0.0f, gripOpen = 10.0f;
  bool holdsMug = false;

  if (robotState == MOVE_TO_PICKUP) {
    float f = stateTimer / 1.0f;
    baseRot = glm::mix(0.0f, -40.0f, f);
    shoulderAngle = glm::mix(-15.0f, -50.0f, f);
  } else if (robotState == GRIP) {
    baseRot = -40.0f;
    shoulderAngle = -50.0f;
    gripOpen = glm::mix(20.0f, 5.0f, stateTimer / 0.5f);
    holdsMug = true;
  } else if (robotState == MOVE_TO_POUR) {
    float f = stateTimer / 1.5f;
    baseRot = glm::mix(-40.0f, 0.0f, f);
    shoulderAngle = glm::mix(-50.0f, -30.0f, f);
    elbowAngle = glm::mix(-10.0f, -60.0f, f);
    holdsMug = true;
  } else if (robotState == MOVE_TO_HANDOFF) {
    float f = stateTimer / 1.0f;

    // Context-aware handoff target - refined for better reach
    float targetBaseRot = 45.0f;
    float targetShoulder = -12.0f; // More horizontal extension
    if (targetNPCIdx == -1) {      // Player
      if (isSeated) {
        targetBaseRot = 25.0f;
        targetShoulder = -32.0f;
      } // Seated handoff
      else {
        targetBaseRot = 52.0f;
        targetShoulder = -10.0f;
      } // Standing handoff
    }

    baseRot = glm::mix(0.0f, targetBaseRot, f);
    shoulderAngle = glm::mix(-30.0f, targetShoulder, f);
    elbowAngle = glm::mix(-60.0f, 0.0f, f); // Straighten elbow more
    holdsMug = true;

  } else if (robotState == RELEASE) {
    // 0.5s Pause logic before actually opening the grip
    if (stateTimer < 0.5f) {
      gripOpen = 5.0f; // Stay closed
    } else {
      gripOpen = glm::mix(5.0f, 20.0f, (stateTimer - 0.5f) / 0.5f);
    }
    holdsMug = (stateTimer < 0.75f); // Visually "handed over" mid-release
  } else if (robotState == HOME) {
    if (stateTimer > 1.0f) {
      robotState = IDLE;
      stateTimer = 0.0f;
    }
  }

  shader.setBool("uUseTexture", false);

  // 0. Support Base (Integrated Mounting to Coffee Machine counter)
  // Structural Mounting Base (bracket overlapping machine chassis)
  glm::mat4 mountingBase = glm::mat4(1.0f);
  mountingBase =
      glm::translate(mountingBase, basePos + glm::vec3(-0.05f, -0.05f, 0.1f));
  mountingBase = glm::scale(mountingBase, glm::vec3(0.8f, 0.12f, 0.55f));
  shader.setMat4("model", mountingBase);
  shader.setV4("baseColor", glm::vec4(0.25f, 0.25f, 0.28f, 1.0f));
  glBindVertexArray(cubeVAO);
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // 1. Base Platform (Turntable)
  glm::mat4 base = glm::mat4(1.0f);
  base = glm::translate(base, basePos);
  base = customRotate(base, glm::radians(baseRot), glm::vec3(0, 1, 0));

  glm::mat4 baseM = glm::scale(base, glm::vec3(0.4f, 0.15f, 0.4f));
  shader.setMat4("model", baseM);
  shader.setV4("baseColor", glm::vec4(0.3f, 0.3f, 0.35f, 1.0f));
  cylinder.draw();

  // 2. Lower Arm (shoulder joint)
  glm::mat4 shoulder = glm::translate(base, glm::vec3(0.0f, 0.1f, 0.0f));
  shoulder =
      customRotate(shoulder, glm::radians(shoulderAngle), glm::vec3(0, 0, 1));

  glm::mat4 lowerArm = glm::translate(shoulder, glm::vec3(0.0f, 0.4f, 0.0f));
  glm::mat4 lowerArmDraw = glm::scale(lowerArm, glm::vec3(0.08f, 0.8f, 0.08f));
  shader.setMat4("model", lowerArmDraw);
  shader.setV4("baseColor", glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));
  glBindVertexArray(cubeVAO);
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // Joint sphere
  glm::mat4 jointM = glm::translate(shoulder, glm::vec3(0, 0, 0));
  jointM = glm::scale(jointM, glm::vec3(0.1f));
  shader.setMat4("model", jointM);
  shader.setV4("baseColor", glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
  cylinder.draw();

  // 3. Upper Arm (elbow joint)
  glm::mat4 elbow = glm::translate(shoulder, glm::vec3(0.0f, 0.8f, 0.0f));
  elbow = customRotate(elbow, glm::radians(elbowAngle), glm::vec3(0, 0, 1));

  glm::mat4 upperArm = glm::translate(elbow, glm::vec3(0.0f, 0.3f, 0.0f));
  glm::mat4 upperArmDraw = glm::scale(upperArm, glm::vec3(0.07f, 0.6f, 0.07f));
  shader.setMat4("model", upperArmDraw);
  shader.setV4("baseColor", glm::vec4(0.6f, 0.6f, 0.65f, 1.0f));
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // Elbow joint sphere
  jointM = glm::translate(elbow, glm::vec3(0, 0, 0));
  jointM = glm::scale(jointM, glm::vec3(0.08f));
  shader.setMat4("model", jointM);
  shader.setV4("baseColor", glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));
  cylinder.draw();

  // 4. Wrist
  glm::mat4 wrist = glm::translate(elbow, glm::vec3(0.0f, 0.6f, 0.0f));
  wrist = customRotate(wrist, glm::radians(wristAngle), glm::vec3(1, 0, 0));

  glm::mat4 wristDraw = glm::scale(wrist, glm::vec3(0.06f, 0.15f, 0.06f));
  shader.setMat4("model", wristDraw);
  shader.setV4("baseColor", glm::vec4(0.5f, 0.5f, 0.55f, 1.0f));
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // 5. Gripper Fingers & Midpoint Capture
  glm::vec3 leftFingerPos(0.0f);
  glm::vec3 rightFingerPos(0.0f);

  for (int f = 0; f < 2; ++f) {
    float side = (f == 0) ? -1.0f : 1.0f;
    glm::mat4 finger =
        glm::translate(wrist, glm::vec3(side * 0.04f, 0.1f, 0.0f));
    finger =
        customRotate(finger, glm::radians(side * gripOpen), glm::vec3(0, 0, 1));

    // Store finger center for midpoint - using offset along the finger length
    glm::vec4 centerLocal = finger * glm::vec4(0.0f, 0.08f, 0.0f, 1.0f);
    if (f == 0)
      leftFingerPos = glm::vec3(centerLocal);
    else
      rightFingerPos = glm::vec3(centerLocal);

    finger = glm::translate(finger, glm::vec3(0.0f, 0.08f, 0.0f));
    finger = glm::scale(finger, glm::vec3(0.02f, 0.16f, 0.04f));
    shader.setMat4("model", finger);
    shader.setV4("baseColor", glm::vec4(0.4f, 0.4f, 0.45f, 1.0f));
    glDrawArrays(GL_TRIANGLES, 0, 36);
  }

  // 6. Held Mug (Coffee Glass) - Anchored to Gripper Midpoint
  if (holdsMug) {
    // Zero-gap pinch: use midpoint between fingers
    glm::vec3 midpoint = (leftFingerPos + rightFingerPos) * 0.5f;
    glm::mat4 mugM = glm::translate(glm::mat4(1.0f), midpoint);

    // Inherit wrist rotation to keep mug vertical-ish relative to arm
    mugM = customRotate(mugM, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mugM = glm::scale(mugM, glm::vec3(0.28f, 0.28f, 0.28f));

    shader.setMat4("model", mugM);
    shader.setV4("baseColor", glm::vec4(0.95f, 0.9f, 0.85f, 1.0f));

    applyTexModeToShader(shader);
    if (gTexMode != TEX_OFF) bindTex0(shader, waterTexture, 0);
    
    glBindVertexArray(coffeeVAO);
    glDrawElements(GL_TRIANGLES, coffeeIndexCount, GL_UNSIGNED_INT, 0);
    shader.setBool("uUseTexture", false);
  }

}

// ================= FRACTAL TREES =================
void drawFractalBranch(Shader &shader, unsigned int cubeVAO,
                       glm::mat4 parentModel, int depth, float length,
                       float thickness) {
  if (depth <= 0 || length < 0.05f)
    return;

  shader.setBool("uUseTexture", false);

  // Draw trunk/branch
  glm::mat4 branchM =
      glm::translate(parentModel, glm::vec3(0, length / 2.0f, 0));
  glm::mat4 draw = glm::scale(branchM, glm::vec3(thickness, length, thickness));
  shader.setMat4("model", draw);
  if (depth > 2)
    shader.setV4("baseColor",
                 glm::vec4(0.35f, 0.2f, 0.1f, 1.0f)); // Brown trunk
  else
    shader.setV4("baseColor", glm::vec4(0.15f, 0.5f + depth * 0.1f, 0.1f,
                                        1.0f)); // Green leaves
  glBindVertexArray(cubeVAO);
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // Branch tip
  glm::mat4 tip = glm::translate(parentModel, glm::vec3(0, length, 0));

  // Sub-branches (fractal recursion with deterministic variation)
  // Upgraded to volumetric 3D branching with 4 branches per level
  for (int i = 0; i < 4; ++i) {
    // Use deterministic "pseudo-random" based on depth and branch index
    float seed = (float)(depth * 7 + i * 13);
    float angleY = i * 90.0f + (fmod(seed * 123.456f, 25.0f));
    float tilt = 20.0f + (fmod(seed * 456.789f, 20.0f));

    glm::mat4 child =
        customRotate(tip, glm::radians(angleY), glm::vec3(0, 1, 0));
    child = customRotate(child, glm::radians(tilt), glm::vec3(1, 0, 0));

    drawFractalBranch(shader, cubeVAO, child, depth - 1, length * 0.7f,
                      thickness * 0.65f);
  }
}

void drawFractalTree(Shader &shader, unsigned int cubeVAO, glm::vec3 pos) {
  glm::mat4 base = glm::mat4(1.0f);
  base = glm::translate(base, pos);
  drawFractalBranch(shader, cubeVAO, base, 5, 2.0f, 0.15f);
}

// ================= SKY BIRDS =================
void updateBirds(float dt) {
  for (auto &b : skyBirds) {
    b.courseAngle +=
        (float)(sin((float)glfwGetTime() * 0.3f + b.flapOffset) * 0.5f);
    float rad = glm::radians(b.courseAngle);
    b.pos.x += (float)(cos(rad) * b.speed * dt);
    b.pos.z += (float)(sin(rad) * b.speed * dt);
    // Keep birds in a controlled height band above the cafe roofline (~15-25
    // range)
    b.pos.y =
        18.0f + (float)sin((float)glfwGetTime() * 0.5f + b.flapOffset) * 4.0f;

    // Tight wrap within bounds focused on cafe presentation area
    if (b.pos.x > 30.0f)
      b.pos.x = -30.0f;
    if (b.pos.x < -30.0f)
      b.pos.x = 30.0f;
    if (b.pos.z > 25.0f)
      b.pos.z = -35.0f;
    if (b.pos.z < -35.0f)
      b.pos.z = 25.0f;
  }
}

void drawBirds(Shader &shader, unsigned int cubeVAO) {
  shader.setBool("uUseTexture", false);
  for (auto &b : skyBirds) {
    float flapAngle =
        (float)sin((float)glfwGetTime() * 8.0f + b.flapOffset) * 30.0f;
    float rad = glm::radians(b.courseAngle);

    glm::mat4 base = glm::mat4(1.0f);
    base = glm::translate(base, b.pos);
    base = customRotate(base, rad, glm::vec3(0, 1, 0));

    // Body
    glm::mat4 body = glm::scale(base, glm::vec3(0.1f, 0.08f, 0.3f));
    shader.setMat4("model", body);
    shader.setV4("baseColor", glm::vec4(0.1f, 0.1f, 0.12f, 1.0f));
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    // Left Wing
    glm::mat4 lWing = glm::translate(base, glm::vec3(-0.15f, 0.0f, 0.0f));
    lWing = customRotate(lWing, glm::radians(flapAngle), glm::vec3(0, 0, 1));
    lWing = glm::translate(lWing, glm::vec3(-0.15f, 0.0f, 0.0f));
    lWing = glm::scale(lWing, glm::vec3(0.3f, 0.03f, 0.2f));
    shader.setMat4("model", lWing);
    shader.setV4("baseColor", glm::vec4(0.15f, 0.15f, 0.18f, 1.0f));
    glDrawArrays(GL_TRIANGLES, 0, 36);

    // Right Wing
    glm::mat4 rWing = glm::translate(base, glm::vec3(0.15f, 0.0f, 0.0f));
    rWing = customRotate(rWing, glm::radians(-flapAngle), glm::vec3(0, 0, 1));
    rWing = glm::translate(rWing, glm::vec3(0.15f, 0.0f, 0.0f));
    rWing = glm::scale(rWing, glm::vec3(0.3f, 0.03f, 0.2f));
    shader.setMat4("model", rWing);
    glDrawArrays(GL_TRIANGLES, 0, 36);
  }
}

// Full Scene

void drawSplineSign(Shader &shader, unsigned int cubeVAO) {
  if (splineVertexCount == 0)
    return;
  shader.setBool("uUseTexture", false);
  shader.setV4("baseColor",
               glm::vec4(0.22f, 0.22f, 0.22f, 1.0f)); // Iron pole look

  glm::mat4 worldBase = glm::mat4(1.0f);
  worldBase = glm::translate(worldBase, glm::vec3(-2.0f, 0.5f, 21.0f));

  // Render spline with thickness: draw small 3D cubes along the path strip
  std::vector<glm::vec3> ctrl = {
      glm::vec3(-2.0f, 4.0f, 2.0f), glm::vec3(-1.0f, 5.0f, 2.0f),
      glm::vec3(1.0f, 5.0f, 2.0f),  glm::vec3(2.0f, 4.0f, 2.0f),
      glm::vec3(1.0f, 3.0f, 2.0f),  glm::vec3(0.0f, 3.5f, 2.0f)};

  glBindVertexArray(cubeVAO);
  int stepsPerSegment = 20;
  for (size_t i = 0; i < ctrl.size() - 3; ++i) {
    for (int j = 0; j <= stepsPerSegment; ++j) {
      float t = (float)j / (float)stepsPerSegment;
      glm::vec3 p =
          catmullRom(ctrl[i], ctrl[i + 1], ctrl[i + 2], ctrl[i + 3], t);
      glm::mat4 segM = glm::translate(worldBase, p);
      segM = glm::scale(segM, glm::vec3(0.08f, 0.08f, 0.08f)); // Thickness
      shader.setMat4("model", segM);
      glDrawArrays(GL_TRIANGLES, 0, 36);
    }
  }

  // Fixed Sign hanging from the end of the spline
  glm::vec3 signPos = catmullRom(ctrl[2], ctrl[3], ctrl[4], ctrl[5], 1.0f);
  glm::mat4 signM = glm::translate(worldBase, signPos);
  signM = glm::scale(signM, glm::vec3(0.8f, 0.5f, 0.05f));
  shader.setMat4("model", signM);
  shader.setV4("baseColor",
               glm::vec4(0.42f, 0.28f, 0.12f, 1.0f)); // Richer wooden sign
  glDrawArrays(GL_TRIANGLES, 0, 36);
}

void drawVisibleSun(Shader &shader, Sphere &sphere) {
  shader.setBool("uIsEmissiveObject", true);
  shader.setBool("uUseTexture", false);

  // Sun
  glm::mat4 sunM = glm::mat4(1.0f);
  sunM = glm::translate(sunM, worldSunDir * 40.0f); // Placed far in the sky
  sunM = glm::scale(sunM, glm::vec3(3.0f));
  shader.setMat4("model", sunM);
  shader.setV4("baseColor", glm::vec4(worldSunColor, 1.0f));
  sphere.draw();

  // Moon (opposite to sun)
  glm::mat4 moonM = glm::mat4(1.0f);
  moonM = glm::translate(moonM, -worldSunDir * 40.0f);
  moonM = glm::scale(moonM, glm::vec3(1.5f));
  shader.setMat4("model", moonM);
  shader.setV4("baseColor", glm::vec4(0.6f, 0.6f, 0.7f, 1.0f));
  sphere.draw();

  shader.setBool("uIsEmissiveObject", false);
}

void drawRiversideScene(Shader &shader, Sphere &sphere, Cylinder &cylinder,
                        unsigned int cubeVAO, float time) {
  glm::mat4 model;

  shader.setBool("isDeck", false);
  shader.setBool("isSky", false);
  shader.setBool("isWater", false);

  // ---------- SKY ----------
  shader.setBool("isSky", true);
  shader.setBool("uUseTexture", false); // IMPORTANT: don't texture sky
  shader.setInt("uComputeMode", 1);

  // Sky color interpolate based on ambient intensity for day/night
  glm::vec3 skyTop = worldSunColor * 0.8f;
  glm::vec3 skyBot = worldSunColor * 1.1f;
  shader.setV4("skyTop", glm::vec4(skyTop, 1.0f));
  shader.setV4("skyBottom", glm::vec4(skyBot, 1.0f));

  drawCube(shader, cubeVAO, glm::vec3(0, 30, -85), glm::vec3(400, 300, 1),
           glm::vec4(1.0f), 0);
  shader.setBool("isSky", false);

  drawVisibleSun(shader, sphere);
  // drawSplineSign(shader, cubeVAO); // Removed as requested (was on trees)


  // ---------- WATER ----------
  shader.setBool("isWater", true);
  shader.setBool("uUseTexture",
                 false); // IMPORTANT: don't texture water in this look
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

  // 1. Back floor (behind main cafe)
  drawCube(shader, cubeVAO, glm::vec3(0, floorY, -20.0f),
           glm::vec3(18.0f, floorH, 14.0f), floorTop, woodTexture);

  // 2. Main Deck Floor (Under the canopies)
  drawCube(shader, cubeVAO, glm::vec3(0.0f, floorY, -5.0f),
           glm::vec3(39.5f, floorH, 16.5f), floorTop, woodTexture);

  // 3. Walkway Entrance Floor (Leading from outside)
  drawCube(shader, cubeVAO, glm::vec3(0.0f, floorY, 12.5f),
           glm::vec3(3.2f, floorH, 19.5f), floorTop, woodTexture);

  // ---------- CAFE PLATFORM / STILTS / UNDERSIDE ----------
  glm::vec4 stiltColor = glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
  glm::vec4 undersideColor = glm::vec4(0.1f, 0.1f, 0.12f, 1.0f);

  // Main deck stilts (6 pillars)
  glm::vec3 stilts[] = {
      glm::vec3(-18.0f, -1.0f, -11.0f), glm::vec3(18.0f, -1.0f, -11.0f),
      glm::vec3(-18.0f, -1.0f, 1.0f),   glm::vec3(18.0f, -1.0f, 1.0f),
      glm::vec3(-8.0f, -1.0f, -25.0f),  glm::vec3(8.0f, -1.0f, -25.0f)};
  for (int i = 0; i < 6; ++i) {
    drawCube(shader, cubeVAO, stilts[i], glm::vec3(0.8f, 3.0f, 0.8f),
             stiltColor, 0);
  }
  // Walkway stilts (4 pillars)
  for (int i = 0; i < 2; ++i) {
    float xS = (i == 0) ? -1.4f : 1.4f;
    drawCube(shader, cubeVAO, glm::vec3(xS, -1.0f, 6.0f),
             glm::vec3(0.3f, 3.0f, 0.3f), stiltColor, 0);
    drawCube(shader, cubeVAO, glm::vec3(xS, -1.0f, 18.0f),
             glm::vec3(0.3f, 3.0f, 0.3f), stiltColor, 0);
  }

  // Underside "Frames" for extra thickness
  drawCube(shader, cubeVAO, glm::vec3(0, floorY - 0.5f, -5.0f),
           glm::vec3(39.5f, 0.1f, 16.5f), undersideColor, 0);
  drawCube(shader, cubeVAO, glm::vec3(0, floorY - 0.5f, 12.5f),
           glm::vec3(3.2f, 0.1f, 19.5f), undersideColor, 0);

  // ---------- Canopy frames ----------
  glm::vec3 frameCenters[] = {glm::vec3(-10.5f, floorY, -5.0f),
                              glm::vec3(0.0f, floorY, -20.0f),
                              glm::vec3(10.5f, floorY, -5.0f)};

  glm::vec4 frameColor = glm::vec4(0.18f, 0.19f, 0.22f, 1.0f);
  glm::vec4 glassColor = glm::vec4(0.72f, 0.86f, 1.0f, 0.18f);

  for (int p = 0; p < 3; ++p) {
    glm::vec3 offset = frameCenters[p];
    float fW = (p == 1) ? 6.5f : 9.0f;
    float fD = (p == 1) ? 5.5f : 6.8f;

    glm::vec3 corners[] = {
        offset + glm::vec3(-fW, 2.5f, -fD), offset + glm::vec3(fW, 2.5f, -fD),
        offset + glm::vec3(-fW, 2.5f, fD), offset + glm::vec3(fW, 2.5f, fD)};

    for (int i = 0; i < 4; ++i)
      drawCube(shader, cubeVAO, corners[i], glm::vec3(0.2f, 5.0f, 0.2f),
               frameColor, 0);

    float bT = 0.15f;
    drawCube(shader, cubeVAO, offset + glm::vec3(0, 4.9f, -fD),
             glm::vec3(fW * 2.1f, bT, bT), frameColor, 0);
    drawCube(shader, cubeVAO, offset + glm::vec3(0, 4.9f, fD),
             glm::vec3(fW * 2.1f, bT, bT), frameColor, 0);
    drawCube(shader, cubeVAO, offset + glm::vec3(-fW, 4.9f, 0),
             glm::vec3(bT, bT, fD * 2.1f), frameColor, 0);
    drawCube(shader, cubeVAO, offset + glm::vec3(fW, 4.9f, 0),
             glm::vec3(bT, bT, fD * 2.1f), frameColor, 0);

    // Bulbs (no texture)
    shader.setBool("uIsEmissiveObject", true);
    for (int i = 0; i < 4; ++i) {
      float x = -fW + (i * (fW * 2.0f) / 3.0f);

      shader.setBool("uUseTexture", false);
      shader.setInt("uComputeMode", 1);

      glm::mat4 modelBulb = glm::mat4(1.0f);
      modelBulb =
          glm::translate(modelBulb, offset + glm::vec3(x, 4.82f, -fD + 0.1f));
      modelBulb = glm::scale(modelBulb, glm::vec3(0.25f));
      shader.setMat4("model", modelBulb);
      shader.setV4("baseColor", glm::vec4(1.0f, 0.88f, 0.55f, 1.0f));
      sphere.draw();

      modelBulb = glm::mat4(1.0f);
      modelBulb =
          glm::translate(modelBulb, offset + glm::vec3(x, 4.82f, fD - 0.1f));
      modelBulb = glm::scale(modelBulb, glm::vec3(0.25f));
      shader.setMat4("model", modelBulb);
      shader.setV4("baseColor", glm::vec4(0.98f, 0.95f, 0.55f, 1.0f));
      sphere.draw();
    }
    shader.setBool("uIsEmissiveObject", false);

    // Furniture (textured wood)
    if (p == 1) {
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(-3.2f, 0, 0), offset.z, 0);
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(3.2f, 0, 0), offset.z, 1);
    } else if (p == 0) {
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(-3.8f, 0, -3.2f), offset.z, 2);
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(3.8f, 0, -3.2f), offset.z, 3);
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(-3.8f, 0, 3.2f), offset.z, 4);
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(3.8f, 0, 3.2f), offset.z, 5);
    } else {
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(-3.8f, 0, -3.2f), offset.z, 6);
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(3.8f, 0, -3.2f), offset.z, 7);
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(-3.8f, 0, 3.2f), offset.z, 8);
      drawStylizedTableSet(shader, sphere, cylinder, cubeVAO,
                           offset + glm::vec3(3.8f, 0, 3.2f), offset.z, 9);
    }

  }

  // ---------- Glass Walls (use canopyTexture) ----------
  for (int p = 0; p < 3; ++p) {
    glm::vec3 offset = frameCenters[p];
    float fW = (p == 1) ? 6.5f : 9.0f;
    float fD = (p == 1) ? 5.5f : 6.8f;

    if (p == 1) {
      drawCube(shader, cubeVAO, offset + glm::vec3(0.0f, 2.5f, -fD),
               glm::vec3(fW * 2.0f, 4.8f, 0.04f), glassColor, canopyTexture);
      drawCube(shader, cubeVAO, offset + glm::vec3(-fW, 2.5f, 0.0f),
               glm::vec3(0.04f, 4.8f, fD * 2.0f), glassColor, canopyTexture);
      drawCube(shader, cubeVAO, offset + glm::vec3(fW, 2.5f, 0.0f),
               glm::vec3(0.04f, 4.8f, fD * 2.0f), glassColor, canopyTexture);
    } else {
      drawCube(shader, cubeVAO, offset + glm::vec3(0.0f, 2.5f, -fD),
               glm::vec3(fW * 2.0f, 4.8f, 0.04f), glassColor, canopyTexture);
      drawCube(shader, cubeVAO, offset + glm::vec3(0.0f, 2.5f, fD),
               glm::vec3(fW * 2.0f, 4.8f, 0.04f), glassColor, canopyTexture);
    }
  }

  // Railings

  // Railings
  glm::vec4 railGlass = glm::vec4(0.70f, 0.85f, 1.0f, 0.45f);
  drawCube(shader, cubeVAO, glm::vec3(-1.55f, 1.2f, 12.5f),
           glm::vec3(0.02f, 1.0f, 19.0f), railGlass, canopyTexture);
  drawCube(shader, cubeVAO, glm::vec3(1.55f, 1.2f, 12.5f),
           glm::vec3(0.02f, 1.0f, 19.0f), railGlass, canopyTexture);

  glm::vec4 capColor = glm::vec4(0.92f, 0.93f, 0.91f, 1.0f);
  drawCube(shader, cubeVAO, glm::vec3(-1.55f, 1.7f, 12.5f),
           glm::vec3(0.06f, 0.06f, 19.0f), capColor, 0);
  drawCube(shader, cubeVAO, glm::vec3(1.55f, 1.7f, 12.5f),
           glm::vec3(0.06f, 0.06f, 19.0f), capColor, 0);

  for (int i = 0; i < 6; ++i) {
    float z = 3.0f + i * 3.84f;
    drawCube(shader, cubeVAO, glm::vec3(-1.55f, 1.0f, z),
             glm::vec3(0.04f, 0.6f, 0.04f), capColor, 0);
    drawCube(shader, cubeVAO, glm::vec3(1.55f, 1.0f, z),
             glm::vec3(0.04f, 0.6f, 0.04f), capColor, 0);
  }

  // ================= PHASE 4 SCENE ATTACHMENTS =================

  // 1. 3x Ceiling Fans (Anchored to the center of each of the 3 dining
  // canopies)
  drawFan(shader, cylinder, cubeVAO, glm::vec3(-10.5f, 4.6f, -5.0f));
  drawFan(shader, cylinder, cubeVAO, glm::vec3(0.0f, 4.6f, -20.0f));
  drawFan(shader, cylinder, cubeVAO, glm::vec3(10.5f, 4.6f, -5.0f));

  // 2. Glass Door (Hinged tightly at the left frame of the entrance threshold)
  drawDoor(shader, cubeVAO, glm::vec3(-1.5f, 0.3f, 1.8f));

  // 3. Wall Clock (Mounted on the back glass wall, offset slightly to kill
  // Z-fighting)
  drawClock(shader, cylinder, cubeVAO, glm::vec3(0.0f, 3.5f, -25.2f));

  // 4. Coffee Machine (Placed on the floor/table at the back wall)
  drawCoffeeMachine(shader, cubeVAO, cylinder, glm::vec3(0.0f, 1.2f, -19.0f));

  // 5. Robotic Serving Arm (physically mounted on coffee machine side as a
  // bracket)
  drawRoboticArm(shader, cylinder, cubeVAO, glm::vec3(0.55f, 1.22f, -19.0f));

  // 11. Cafe Sign (3D block letters above door)
  drawCafeSign(shader, cubeVAO);

  // 6. Fractal Trees (riverside environment)
  drawFractalTree(shader, cubeVAO, glm::vec3(-8.0f, -0.5f, 18.0f));
  drawFractalTree(shader, cubeVAO, glm::vec3(8.0f, -0.5f, 18.0f));
  drawFractalTree(shader, cubeVAO, glm::vec3(-12.0f, -0.5f, 22.0f));
  drawFractalTree(shader, cubeVAO, glm::vec3(12.0f, -0.5f, 22.0f));
  drawFractalTree(shader, cubeVAO, glm::vec3(0.0f, -0.5f, 26.0f));

  // 7. Sky Birds
  drawBirds(shader, cubeVAO);

  // 8. Ruled Surface Arch (decorative entrance arch - RESTORED)
  shader.setBool("uUseTexture", false);
  glm::mat4 archM = glm::mat4(1.0f);
  archM = glm::translate(archM, glm::vec3(0.0f, 7.2f, 1.8f));
  archM = customRotate(archM, glm::radians(90.0f), glm::vec3(0, 0, 1));
  archM = glm::scale(archM, glm::vec3(0.25f, 0.25f, 0.4f));
  shader.setMat4("model", archM);
  shader.setV4("baseColor", glm::vec4(0.7f, 0.55f, 0.3f, 1.0f));
  glBindVertexArray(ruledVAO);
  glDrawElements(GL_TRIANGLES, ruledIndexCount, GL_UNSIGNED_INT, 0);



  // 9. Decorative Spheres (on entrance railing posts, not on door)
  applyTexModeToShader(shader);
  if (gTexMode != TEX_OFF)
    bindTex0(shader, waterTexture, 0);
  else
    shader.setBool("uUseTexture", false);
  // Left post sphere
  glm::mat4 ornM = glm::mat4(1.0f);
  ornM = glm::translate(ornM, glm::vec3(-1.55f, 2.15f, 3.0f));
  ornM = glm::scale(ornM, glm::vec3(0.18f));
  shader.setMat4("model", ornM);
  shader.setV4("baseColor", glm::vec4(0.9f, 0.7f, 0.3f, 1.0f));
  sphere.draw();
  // Right post sphere
  ornM = glm::mat4(1.0f);
  ornM = glm::translate(ornM, glm::vec3(1.55f, 2.15f, 3.0f));
  ornM = glm::scale(ornM, glm::vec3(0.18f));
  shader.setMat4("model", ornM);
  shader.setV4("baseColor", glm::vec4(0.9f, 0.7f, 0.3f, 1.0f));
  sphere.draw();

  // 10. Decorative Cone (café entrance bollard/post-top)
  shader.setBool("uUseTexture", false);
  glm::mat4 coneM = glm::mat4(1.0f);
  coneM = glm::translate(coneM, glm::vec3(-1.55f, 1.9f, 3.0f));
  coneM = glm::scale(coneM, glm::vec3(0.12f, 0.25f, 0.12f));
  shader.setMat4("model", coneM);
  shader.setV4("baseColor", glm::vec4(0.8f, 0.4f, 0.2f, 1.0f));
  gCone.draw();
  coneM = glm::mat4(1.0f);
  coneM = glm::translate(coneM, glm::vec3(1.55f, 1.9f, 3.0f));
  coneM = glm::scale(coneM, glm::vec3(0.12f, 0.25f, 0.12f));
  shader.setMat4("model", coneM);
  gCone.draw();
}

// MAIN
int main() {
  srand((unsigned int)time(NULL)); // Seed RNG
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow *window = glfwCreateWindow(
      SCR_WIDTH, SCR_HEIGHT, "Cafe Beel Harina - 3D Riverside", NULL, NULL);
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

  initGeometricAssets(); // Build Bezier coffee glass, ruled surface arch, bird
                         // entities

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
  glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices,
               GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  Sphere sphere(1.0f, 32, 16);
  Cylinder planter(1.0f, 1.0f, 1.0f, 16, 1);

  // Build cone
  gCone.build(40);

  // TEXTURES

  // TEXTURES (RELATIVE PATHS WITH FALLBACK)
  // Corrected to actual filenames found in developmental Lab_4 folder
  woodTexture = loadTexture("container2.png", wrapModes[currentWrap],
                            wrapModes[currentWrap], filterModes[currentFilter],
                            filterModes[currentFilter]);
  canopyTexture = loadTexture(
      "container2_specular.png", wrapModes[currentWrap], wrapModes[currentWrap],
      filterModes[currentFilter], filterModes[currentFilter]);
  waterTexture =
      loadTexture("emoji.png", wrapModes[currentWrap], wrapModes[currentWrap],
                  filterModes[currentFilter], filterModes[currentFilter]);

  std::cout << "\n============================================\n";
  std::cout << " CAFE BEEL HARINA - FINAL ASSIGNMENT BUILD  \n";
  std::cout << "============================================\n";
  std::cout << " [MODES] I: Walk/Fly | V: 4-Way Viewport\n";
  std::cout << " [MOVE]  W/S/A/D: Move | E/R: Vertical\n";
  std::cout << " [CAM]   X/Y/Z: Rotation | F: Orbit Look-At\n";
  std::cout << "         C: Cycle Camera | H: Fullscreen\n";
  std::cout << "--------------------------------------------\n";
  std::cout << " [DOOR]   K: Open/Close Door (Proximity)\n";
  std::cout << " [COFFEE] B: Machine + Robot Serving (Prox)\n";
  std::cout << " [CHAIR]  Q: Sit/Stand Alignment (Prox)\n";
  std::cout << " [SIGN]   U: Cafe Sign Glow ON/OFF\n";
  std::cout << " [GEOM]   Spline Sign (Entry) | Ruled Arch\n";
  std::cout << "--------------------------------------------\n";
  std::cout << " [LIGHT]  1: Sun | 2: Point | 3: Spot\n";
  std::cout << "          4: Emissive | 5: Ambient\n";
  std::cout << "          6: Diffuse | 7: Specular\n";
  std::cout << "          L: Master | G: Ceiling Fan\n";
  std::cout << " [TIME]   100s Day/Night Cycle + Visible Sun\n";
  std::cout << "--------------------------------------------\n";
  std::cout << " P: Wireframe | T: Wrap | M: Filter\n";
  std::cout << " 8: TEX_OFF | 9: TEX_SIMPLE\n";
  std::cout << " 0: TEX_BLEND_VERTEX | -: TEX_BLEND_FRAGMENT\n";
  std::cout << "============================================\n\n";
  initSeats();
  while (!glfwWindowShouldClose(window)) {
    float currentFrame = (float)glfwGetTime();
    deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    // --- DYNAMIC PHASE 4 MATH ---
    if (isFanOn) {
      fanAngle += deltaTime * 200.0f;
      if (fanAngle >= 360.0f)
        fanAngle -= 360.0f;
    }
    // Door Animation & Auto-Close Logic
    if (isDoorOpen) {
      doorAutoCloseTimer += deltaTime;
      if (doorAutoCloseTimer > 5.0f) {
        playerWantsDoorOpen = false;
        // anyNPCNearDoor will keep isDoorOpen true if they are still there
      }
    } else {
      doorAutoCloseTimer = 0.0f;
    }

    float targetDoor =
        isDoorOpen
            ? (90.0f * doorSwingDirection)
            : 0.0f; // Open inwards/outwards
    doorAngle += (targetDoor - doorAngle) * deltaTime * 5.0f;



    float targetBrew = isBrewing ? -45.0f : 0.0f;
    brewAngle += (targetBrew - brewAngle) * deltaTime * 10.0f;

    // --- 100s Automatic Day/Night Cycle ---
    dayCycleTime += deltaTime;
    if (dayCycleTime >= DAY_DURATION)
      dayCycleTime -= DAY_DURATION;

    float dayT = dayCycleTime / DAY_DURATION; // 0.0 to 1.0
    float sunAngle = dayT * 2.0f * 3.14159f;
    worldSunDir = glm::vec3((float)sin(sunAngle), (float)-cos(sunAngle),
                            0.5f); // Rotating sun

    if (dayT < 0.1f) { // Dawn
      worldSunColor = glm::mix(glm::vec3(0.1f, 0.1f, 0.2f),
                               glm::vec3(1.0f, 0.6f, 0.4f), dayT / 0.1f);
      worldAmbientIntensity = 0.1f + 0.1f * (dayT / 0.1f);
    } else if (dayT < 0.4f) { // Morning -> Noon
      worldSunColor =
          glm::mix(glm::vec3(1.0f, 0.6f, 0.4f), glm::vec3(1.0f, 1.0f, 0.95f),
                   (dayT - 0.1f) / 0.3f);
      worldAmbientIntensity = 0.25f;
    } else if (dayT < 0.7f) { // Afternoon
      worldSunColor =
          glm::mix(glm::vec3(1.0f, 1.0f, 0.95f), glm::vec3(1.0f, 0.7f, 0.3f),
                   (dayT - 0.4f) / 0.3f);
      worldAmbientIntensity = 0.25f;
    } else if (dayT < 0.85f) { // Dusk
      worldSunColor =
          glm::mix(glm::vec3(1.0f, 0.7f, 0.3f), glm::vec3(0.4f, 0.2f, 0.5f),
                   (dayT - 0.7f) / 0.15f);
      worldAmbientIntensity = 0.15f;
    } else { // Night
      worldSunColor =
          glm::mix(glm::vec3(0.4f, 0.2f, 0.5f), glm::vec3(0.05f, 0.05f, 0.15f),
                   (dayT - 0.85f) / 0.15f);
      worldAmbientIntensity = 0.05f;
    }

    // --- Phased Robot Serving State Machine ---
    if (robotState != IDLE) {
      stateTimer += deltaTime;
      if (robotState == MOVE_TO_PICKUP) {
        if (stateTimer > 1.0f) {
          robotState = GRIP;
          stateTimer = 0.0f;
        }
      } else if (robotState == GRIP) {
        if (stateTimer > 0.5f) {
          robotState = MOVE_TO_POUR;
          stateTimer = 0.0f;
          isBrewing = true;
        }
      } else if (robotState == MOVE_TO_POUR) {
        if (stateTimer > 1.5f) {
          robotState = MOVE_TO_HANDOFF;
          stateTimer = 0.0f;
          isBrewing = false;
        }
      } else if (robotState == MOVE_TO_HANDOFF) {
        if (stateTimer > 1.0f) {
          robotState = RELEASE;
          stateTimer = 0.0f;
          if (targetNPCIdx == -1)
            hasCoffee = true;
          else {
            customers[targetNPCIdx].hasReceivedCoffee = true;
            customers[targetNPCIdx].timer = 0.01f;
          }
        }
      } else if (robotState == RELEASE) {
        if (stateTimer > 0.5f) {
          robotState = HOME;
          stateTimer = 0.0f;
        }
      } else if (robotState == HOME) {
        if (stateTimer > 1.0f) {
          robotState = IDLE;
          stateTimer = 0.0f;
          targetNPCIdx = -1;
        }
      }
    }

    updateNPCs(deltaTime);
    updateBirds(deltaTime);
    processInput(window);

    glClearColor(0.55f, 0.75f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ourShader.use();
    glPolygonMode(GL_FRONT_AND_BACK, isWireframe ? GL_LINE : GL_FILL);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    if (height == 0)
      height = 1;

    int halfW = width / 2;
    int halfH = height / 2;
    if (halfH == 0)
      halfH = 1;

    float aspect = (float)halfW / (float)halfH;
    glm::mat4 projection =
        glm::perspective(glm::radians(camera.Zoom), aspect, 0.1f, 100.0f);

    auto renderPass = [&](int vpX, int vpY, int vpW, int vpH,
                          CameraMode evalMode,
                          bool evaluateLookAtOverride = false) {
      glViewport(vpX, vpY, vpW, vpH);

      glm::vec3 effectiveViewPos = camera.Position;
      glm::vec3 effectiveViewFront = camera.Front;
      glm::mat4 view;

      if (evalMode == STATIC) {
        effectiveViewPos = glm::vec3(0, 10, 15);
        effectiveViewFront =
            glm::normalize(glm::vec3(0, 0, 0) - effectiveViewPos);
        view = glm::lookAt(effectiveViewPos, glm::vec3(0, 0, 0),
                           glm::vec3(0, 1, 0));
      } else if (evalMode == TOP) {
        effectiveViewPos = glm::vec3(0, 20, 0.1f);
        effectiveViewFront =
            glm::normalize(glm::vec3(0, 0, 0) - effectiveViewPos);
        view = glm::lookAt(effectiveViewPos, glm::vec3(0, 0, 0),
                           glm::vec3(0, 1, 0));
      } else if (evalMode == BIRD_EYE) {
        effectiveViewPos = glm::vec3(-15.0f, 15.0f, 15.0f);
        effectiveViewFront =
            glm::normalize(glm::vec3(0.0f, 0.0f, -2.0f) - effectiveViewPos);
        view = glm::lookAt(effectiveViewPos, glm::vec3(0.0f, 0.0f, -2.0f),
                           glm::vec3(0, 1, 0));
      } else if (evalMode == ORBIT) {
        float radius = 15.0f;
        float camX = sin((float)glfwGetTime()) * radius;
        float camZ = cos((float)glfwGetTime()) * radius;
        effectiveViewPos = glm::vec3(camX, 5.0f, camZ);
        effectiveViewFront =
            glm::normalize(glm::vec3(0, 0, 0) - effectiveViewPos);
        view = glm::lookAt(effectiveViewPos, glm::vec3(0, 0, 0),
                           glm::vec3(0, 1, 0));
      } else { // FPS
        if (evaluateLookAtOverride && isLookAtOrbiting) {
          float elapsedTime = (float)glfwGetTime() - orbitStartTime;
          float angle = elapsedTime * 1.5f;

          glm::vec3 offset = orbitStartPos - orbitFocalPoint;
          glm::mat4 rot =
              glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
          glm::vec3 spunPos =
              orbitFocalPoint + glm::vec3(rot * glm::vec4(offset, 0.0f));

          effectiveViewPos = spunPos;
          effectiveViewFront = glm::normalize(orbitFocalPoint - spunPos);
          view = glm::lookAt(spunPos, orbitFocalPoint,
                             glm::vec3(0.0f, 1.0f, 0.0f));
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

      // Global Day/Night Lighting
      ourShader.setV3("dirLightDir", worldSunDir);
      ourShader.setV3("dirLightColor", worldSunColor);
      ourShader.setV3("ambientColor", worldSunColor * worldAmbientIntensity *
                                          0.5f); // Soften ambient

      ourShader.setV3("spotLightDir", effectiveViewFront);

      for (int i = 0; i < 4; i++) {
        ourShader.setV3("pointLightPositions[" + std::to_string(i) + "]",
                        canopyPointLights[i]);
      }
      ourShader.setBool("uIsEmissiveObject", false);

      ourShader.setMat4("projection", projection);
      ourShader.setMat4("view", view);

      drawRiversideScene(ourShader, sphere, planter, cubeVAO,
                         (float)glfwGetTime());
      drawNPCs(ourShader, cubeVAO, planter);

      // Phase 7: Carried Coffee HUD
      if (isWalkMode && hasCoffee && evalMode == FPS) {
        glClear(GL_DEPTH_BUFFER_BIT); // Ensure HUD always renders on top
        glm::mat4 hudModel =
            glm::inverse(view); // Stick to camera space natively
        hudModel = glm::translate(
            hudModel,
            glm::vec3(0.55f, -0.45f,
                      -0.8f)); // Pushed deeper into bottom-right periphery
        hudModel = glm::rotate(
            hudModel, glm::radians(15.0f),
            glm::vec3(1.0f, 0.0f, 0.0f)); // Tip base slightly outward
        hudModel =
            glm::rotate(hudModel, glm::radians(-10.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)); // Angle mug inwards
        hudModel = glm::scale(
            hudModel, glm::vec3(0.09f, 0.14f, 0.09f)); // Sleaker visual profile

        ourShader.setV4("baseColor",
                        glm::vec4(0.95f, 0.9f, 0.85f, 1.0f)); // Ceramic white
        
        applyTexModeToShader(ourShader);
        if (gTexMode != TEX_OFF) bindTex0(ourShader, waterTexture, 0);
        
        glBindVertexArray(coffeeVAO);
        glDrawElements(GL_TRIANGLES, coffeeIndexCount, GL_UNSIGNED_INT, 0);
        ourShader.setBool("uUseTexture", false);

      }
    };

    if (isWalkMode) {
      // Walk Mode rigorously enforces single FPS Viewport
      projection = glm::perspective(glm::radians(camera.Zoom),
                                    (float)width / (float)height, 0.1f, 100.0f);
      renderPass(0, 0, width, height, FPS, true);
    } else if (isFourWayView) {
      // Explicit 4-Quadrant Viewport Execution
      renderPass(0, halfH, halfW, halfH, BIRD_EYE); // Top-Left
      renderPass(halfW, halfH, halfW, halfH, TOP);  // Top-Right
      renderPass(0, 0, halfW, halfH, STATIC);       // Bottom-Left
      renderPass(halfW, 0, halfW, halfH, FPS,
                 true); // Bottom-Right (FPS override)
    } else {
      // Single Viewport Execution
      projection = glm::perspective(glm::radians(camera.Zoom),
                                    (float)width / (float)height, 0.1f, 100.0f);
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
void processInput(GLFWwindow *window) {
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);

  static bool keys[1024] = {false};

  glm::vec3 oldPos = camera.Position;

  if (!isSeated) {
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
      camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
      camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
      camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
      camera.ProcessKeyboard(RIGHT, deltaTime);

    if (!isWalkMode) {
      if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        camera.ProcessKeyboard(UP, deltaTime);
      if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
        camera.ProcessKeyboard(DOWN, deltaTime);
    } else {
      // Apply locked height only for FPS mode in walk mode
      if (currentCameraMode == FPS) {
        camera.Position.y = 2.3f;
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
  if (horizontalDistanceXZ(camera.Position, glm::vec3(-1.5f, 0.0f, 1.8f)) <
      2.5f)
    currentZone = 1;
  else if (horizontalDistanceXZ(camera.Position,
                                glm::vec3(0.0f, 0.0f, -19.0f)) < 2.0f)
    currentZone = 2;
  else {
    float bestDist = 1.2f;
    for (int s = 0; s < allSeats.size(); ++s) {
      if (allSeats[s].occupiedBy == 0 || allSeats[s].occupiedBy == 1) {
        float d = horizontalDistanceXZ(camera.Position, allSeats[s].pos);
        if (d < bestDist) {
          bestDist = d;
          nearbySeatIdx = s;
          currentZone = 3;
        }
      }
    }
  }

  static int lastHintZone = 0;
  if (currentZone != lastHintZone) {
    if (currentZone == 1)
      std::cout << "\n[HINT] Press 'K' to interact with the Glass Door\n";
    else if (currentZone == 2)
      std::cout << "\n[HINT] Press 'B' to get Coffee\n";
    else if (currentZone == 3)
      std::cout << "\n[HINT] Press 'Q' to sit at a table\n";
    lastHintZone = currentZone;
  }

  if (!isWalkMode) {
    float pDir = 0, yDir = 0, rDir = 0;
    if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS)
      pDir = 1.0f;
    if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS)
      yDir = 1.0f;
    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
      rDir = 1.0f;
    if (pDir != 0 || yDir != 0 || rDir != 0)
      camera.ProcessRotationKeys(pDir, yDir, rDir, deltaTime);
  }

  auto toggle = [&](int key, bool &val) {
    if (glfwGetKey(window, key) == GLFW_PRESS && !keys[key]) {
      val = !val;
      keys[key] = true;
    } else if (glfwGetKey(window, key) == GLFW_RELEASE)
      keys[key] = false;
  };

  toggle(GLFW_KEY_P, isWireframe);

  // Viewport Toggle
  if (!isWalkMode) {
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS && !keys[GLFW_KEY_V]) {
      isFourWayView = !isFourWayView;
      keys[GLFW_KEY_V] = true;
      std::cout << "Viewport Mode: " << (isFourWayView ? "4-Way" : "Single")
                << "\n";
    } else if (glfwGetKey(window, GLFW_KEY_V) == GLFW_RELEASE)
      keys[GLFW_KEY_V] = false;
  } else {
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS && !keys[GLFW_KEY_V]) {
      keys[GLFW_KEY_V] = true;
      std::cout << "[LOCKED] Switch to Camera Viewer ('I') to use Viewports.\n";
    } else if (glfwGetKey(window, GLFW_KEY_V) == GLFW_RELEASE)
      keys[GLFW_KEY_V] = false;
  }

  // Phase 4 & 5 Dynamic Toggles (Overridden with Phase 5 Proximity)
  if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !keys[GLFW_KEY_G]) {
    isFanOn = !isFanOn;
    keys[GLFW_KEY_G] = true;
    std::cout << "Ceiling Fan: " << (isFanOn ? "ON" : "OFF") << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE)
    keys[GLFW_KEY_G] = false;

  // --- Phase 5: Walk Mode Toggle (I) ---
  if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS && !keys[GLFW_KEY_I]) {
    isWalkMode = !isWalkMode;
    keys[GLFW_KEY_I] = true;
    std::cout << "Walk Mode: " << (isWalkMode ? "ON" : "OFF (Free Fly)")
              << "\n";
    if (isWalkMode) {
      isFourWayView = false;
      isLookAtOrbiting = false;
      currentCameraMode = FPS;
      camera.Position.y = 2.55f;

      camera.Up = glm::vec3(0.0f, 1.0f, 0.0f);
    }
  } else if (glfwGetKey(window, GLFW_KEY_I) == GLFW_RELEASE)
    keys[GLFW_KEY_I] = false;

  // --- Phase 5: Sit Interaction (Q) ---
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS && !keys[GLFW_KEY_Q]) {
    keys[GLFW_KEY_Q] = true;
    if (!isSeated) {
      if (currentZone == 3 && nearbySeatIdx != -1 &&
          allSeats[nearbySeatIdx].occupiedBy == 0) {
        isSeated = true;
        playerSeatIdx = nearbySeatIdx;
        allSeats[nearbySeatIdx].occupiedBy = 1;
        preSeatPos = camera.Position; // Fix: Record position before sitting!
        Seat &s = allSeats[nearbySeatIdx];
        // Seated eye height matched to NPC seated eye level (approx 2.25f)
        camera.Position = glm::vec3(s.pos.x, 2.25f, s.pos.z);

        camera.Yaw = s.facingAngle;
        camera.Pitch = -15.0f;
        camera.ProcessMouseMovement(0.0f, 0.0f, false);
        std::cout << "[INFO] Sat Down. Press 'M' to pick up mug from table.\n";
      }
    } else {
      isSeated = false;
      hasCoffee = false; // Reset coffee state on stand up (assume mug left on table)

      int prevIdx = playerSeatIdx;
      if (playerSeatIdx != -1)
        allSeats[playerSeatIdx].occupiedBy = 0;
      playerSeatIdx = -1;

      // Refined Stand-up Fallback: Place based on seat facing if preSeatPos is
      // blocked
      bool posRestored = false;
      if (isPositionValid(preSeatPos)) {
        camera.Position = preSeatPos;
        posRestored = true;
      } else if (prevIdx != -1) {
        // Compute exit direction based on seat facing (usually move back 1.2
        // units)
        float angle = glm::radians(allSeats[prevIdx].facingAngle);
        glm::vec3 exitDir = glm::vec3(-cos(angle), 0.0f, -sin(angle));
        glm::vec3 fallback = camera.Position + (exitDir * 1.2f);
        fallback.y = 2.55f;

        if (isPositionValid(fallback)) {
          camera.Position = fallback;
          posRestored = true;
        }
      }

      if (!posRestored) {
        // Absolute safety: tiny step back from current camera position
        glm::vec3 lastResort = camera.Position - (camera.Front * 0.5f);
        lastResort.y = 2.55f;
        camera.Position = lastResort;
      }

      camera.Position.y = 2.55f;

      std::cout << "[INFO] Stood Up.\n";
    }

  } else if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_RELEASE)
    keys[GLFW_KEY_Q] = false;

  // --- MUG PICKUP (M key) ---
  if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS && !keys[GLFW_KEY_M]) {
    keys[GLFW_KEY_M] = true;
    if (isSeated && !hasCoffee) {
      hasCoffee = true;
      std::cout << "[INFO] Picked up mug from table.\n";
    }
  } else if (glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE)
    keys[GLFW_KEY_M] = false;


  // --- Door Interaction (K key) ---
  if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS && !keys[GLFW_KEY_K]) {
    keys[GLFW_KEY_K] = true;
    if (horizontalDistanceXZ(camera.Position, glm::vec3(0.0f, 0.0f, 1.8f)) <
        3.0f) {
      playerWantsDoorOpen = !playerWantsDoorOpen;
      std::cout << "Glass Door: "
                << (playerWantsDoorOpen ? "OPENING" : "CLOSING") << "\n";
    } else {
      std::cout << "Move closer to the door\n";
    }
  } else if (glfwGetKey(window, GLFW_KEY_K) == GLFW_RELEASE)
    keys[GLFW_KEY_K] = false;

  // --- Cafe Sign Light Toggle (U key) ---
  if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS && !keys[GLFW_KEY_U]) {
    signLightOn = !signLightOn;
    keys[GLFW_KEY_U] = true;
    std::cout << "Cafe Sign Light: " << (signLightOn ? "ON" : "OFF") << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_U) == GLFW_RELEASE)
    keys[GLFW_KEY_U] = false;

  // --- Phase 5: Proximity Coffee Machine + Robotic Arm ---
  if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS && !keys[GLFW_KEY_B]) {
    keys[GLFW_KEY_B] = true;
    if (horizontalDistanceXZ(camera.Position, glm::vec3(0.0f, 0.0f, -19.0f)) <
        3.5f) {
      if (robotState == IDLE && !hasCoffee) {
        robotState = MOVE_TO_PICKUP;
        targetNPCIdx = -1; // Serve player
        stateTimer = 0.0f;
        isBrewing = true;
        std::cout << "[ROBOT] Robotic arm serving coffee...\n";
      } else if (hasCoffee) {
        std::cout << "[INFO] You already have coffee.\n";
      } else {
        std::cout << "[INFO] Robot is already serving, please wait...\n";
      }
    } else {
      std::cout << "Move closer to the coffee machine\n";
    }
  } else if (glfwGetKey(window, GLFW_KEY_B) == GLFW_RELEASE)
    keys[GLFW_KEY_B] = false;

  // Texture Map Logic
  if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS && !keys[GLFW_KEY_8]) {
    gTexMode = TEX_OFF;
    keys[GLFW_KEY_8] = true;
    std::cout << "TEX_OFF\n";
  } else if (glfwGetKey(window, GLFW_KEY_8) == GLFW_RELEASE)
    keys[GLFW_KEY_8] = false;

  if (glfwGetKey(window, GLFW_KEY_9) == GLFW_PRESS && !keys[GLFW_KEY_9]) {
    gTexMode = TEX_SIMPLE;
    keys[GLFW_KEY_9] = true;
    std::cout << "TEX_SIMPLE\n";
  } else if (glfwGetKey(window, GLFW_KEY_9) == GLFW_RELEASE)
    keys[GLFW_KEY_9] = false;

  if (glfwGetKey(window, GLFW_KEY_0) == GLFW_PRESS && !keys[GLFW_KEY_0]) {
    gTexMode = TEX_BLEND_VERTEX;
    keys[GLFW_KEY_0] = true;
    std::cout << "TEX_BLEND_VERTEX\n";
  } else if (glfwGetKey(window, GLFW_KEY_0) == GLFW_RELEASE)
    keys[GLFW_KEY_0] = false;

  if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS &&
      !keys[GLFW_KEY_MINUS]) {
    gTexMode = TEX_BLEND_FRAGMENT;
    keys[GLFW_KEY_MINUS] = true;
    std::cout << "TEX_BLEND_FRAGMENT\n";
  } else if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_RELEASE)
    keys[GLFW_KEY_MINUS] = false;

  // Lighting Control Logic
  if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS && !keys[GLFW_KEY_1]) {
    dirLightOn = !dirLightOn;
    keys[GLFW_KEY_1] = true;
    std::cout << "Sun (Directional): " << dirLightOn << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_1) == GLFW_RELEASE)
    keys[GLFW_KEY_1] = false;

  if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS && !keys[GLFW_KEY_2]) {
    pointLightOn = !pointLightOn;
    keys[GLFW_KEY_2] = true;
    std::cout << "Canopy Bulbs (Point): " << pointLightOn << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_2) == GLFW_RELEASE)
    keys[GLFW_KEY_2] = false;

  if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS && !keys[GLFW_KEY_3]) {
    spotLightOn = !spotLightOn;
    keys[GLFW_KEY_3] = true;
    std::cout << "Flashlight (Spot): " << spotLightOn << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_3) == GLFW_RELEASE)
    keys[GLFW_KEY_3] = false;

  if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS && !keys[GLFW_KEY_4]) {
    emissiveOn = !emissiveOn;
    keys[GLFW_KEY_4] = true;
    std::cout << "Emissive Glow: " << emissiveOn << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_4) == GLFW_RELEASE)
    keys[GLFW_KEY_4] = false;

  if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS && !keys[GLFW_KEY_5]) {
    ambientOn = !ambientOn;
    keys[GLFW_KEY_5] = true;
    std::cout << "Ambient Bounce: " << ambientOn << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_5) == GLFW_RELEASE)
    keys[GLFW_KEY_5] = false;

  if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS && !keys[GLFW_KEY_6]) {
    diffuseOn = !diffuseOn;
    keys[GLFW_KEY_6] = true;
    std::cout << "Diffuse Map: " << diffuseOn << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_6) == GLFW_RELEASE)
    keys[GLFW_KEY_6] = false;

  if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS && !keys[GLFW_KEY_7]) {
    specularOn = !specularOn;
    keys[GLFW_KEY_7] = true;
    std::cout << "Specular Gloss: " << specularOn << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_7) == GLFW_RELEASE)
    keys[GLFW_KEY_7] = false;

  if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS && !keys[GLFW_KEY_L]) {
    masterLightOn = !masterLightOn;
    keys[GLFW_KEY_L] = true;
    std::cout << "MASTER LIGHT SWITCH: " << masterLightOn << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_L) == GLFW_RELEASE)
    keys[GLFW_KEY_L] = false;

  // Window Toggle (J)
  if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS && !keys[GLFW_KEY_J]) {
    isWindowOpen = !isWindowOpen;
    keys[GLFW_KEY_J] = true;
    std::cout << "Cafe Windows: " << (isWindowOpen ? "OPENING" : "CLOSING")
              << "\n";
  } else if (glfwGetKey(window, GLFW_KEY_J) == GLFW_RELEASE)
    keys[GLFW_KEY_J] = false;

  // Fullscreen Toggle (H)
  static bool isFullscreen = false;
  if (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS && !keys[GLFW_KEY_H]) {
    isFullscreen = !isFullscreen;
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (isFullscreen)
      glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                           mode->refreshRate);
    else
      glfwSetWindowMonitor(window, NULL, 100, 100, SCR_WIDTH, SCR_HEIGHT, 0);
    keys[GLFW_KEY_H] = true;
  } else if (glfwGetKey(window, GLFW_KEY_H) == GLFW_RELEASE)
    keys[GLFW_KEY_H] = false;

  // Look-at Orbit Toggle (F)
  if (!isWalkMode) {
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !keys[GLFW_KEY_F]) {
      isLookAtOrbiting = !isLookAtOrbiting;
      if (isLookAtOrbiting) {
        orbitStartTime = (float)glfwGetTime();
        orbitFocalPoint = camera.Position + (camera.Front * 5.0f);
        orbitStartPos = camera.Position;
      }
      std::cout << "> Focal Pivot Drone Mode: "
                << (isLookAtOrbiting ? "ON\n" : "OFF\n");
      keys[GLFW_KEY_F] = true;
    } else if (glfwGetKey(window, GLFW_KEY_F) == GLFW_RELEASE)
      keys[GLFW_KEY_F] = false;
  } else {
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !keys[GLFW_KEY_F]) {
      keys[GLFW_KEY_F] = true;
      std::cout
          << "[LOCKED] Switch to Camera Viewer ('I') to use Orbit Drone.\n";
    } else if (glfwGetKey(window, GLFW_KEY_F) == GLFW_RELEASE)
      keys[GLFW_KEY_F] = false;
  }

  // cycle camera (C)
  if (!isWalkMode) {
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && !keys[GLFW_KEY_C]) {
      currentCameraMode = (CameraMode)((currentCameraMode + 1) % 5);
      keys[GLFW_KEY_C] = true;
    } else if (glfwGetKey(window, GLFW_KEY_C) == GLFW_RELEASE)
      keys[GLFW_KEY_C] = false;
  } else {
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && !keys[GLFW_KEY_C]) {
      keys[GLFW_KEY_C] = true;
      std::cout << "[LOCKED] Switch to Camera Viewer ('I') to Cycle Views.\n";
    } else if (glfwGetKey(window, GLFW_KEY_C) == GLFW_RELEASE)
      keys[GLFW_KEY_C] = false;
  }

  // wrapping (T)
  if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !keys[GLFW_KEY_T]) {
    currentWrap = (currentWrap + 1) % 3;
    unsigned int texs[] = {woodTexture, waterTexture, canopyTexture};
    for (auto t : texs) {
      glBindTexture(GL_TEXTURE_2D, t);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapModes[currentWrap]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapModes[currentWrap]);
    }
    keys[GLFW_KEY_T] = true;
    std::cout << "Wrap mode changed\n";
  } else if (glfwGetKey(window, GLFW_KEY_T) == GLFW_RELEASE)
    keys[GLFW_KEY_T] = false;

  // filtering (M)
  if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS && !keys[GLFW_KEY_M]) {
    currentFilter = (currentFilter + 1) % 2;
    unsigned int texs[] = {woodTexture, waterTexture, canopyTexture};
    for (auto t : texs) {
      glBindTexture(GL_TEXTURE_2D, t);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      filterModes[currentFilter]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                      filterModes[currentFilter]);
    }
    keys[GLFW_KEY_M] = true;
    std::cout << "Filter mode changed\n";
  } else if (glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE)
    keys[GLFW_KEY_M] = false;
}

void framebuffer_size_callback(GLFWwindow *, int width, int height) {
  glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow *, double xposIn, double yposIn) {
  float xpos = (float)xposIn;
  float ypos = (float)yposIn;

  if (firstMouse) {
    lastX = xpos;
    lastY = ypos;
    firstMouse = false;
  }

  camera.ProcessMouseMovement(xpos - lastX, lastY - ypos);
  lastX = xpos;
  lastY = ypos;
}

void scroll_callback(GLFWwindow *, double, double yoffset) {
  camera.Zoom -= (float)yoffset;
  if (camera.Zoom < 1.0f)
    camera.Zoom = 1.0f;
  if (camera.Zoom > 45.0f)
    camera.Zoom = 45.0f;
}

// TEXTURE LOADER

unsigned int loadTexture(char const *fileName, GLint wrapS, GLint wrapT,
                         GLint minFilter, GLint magFilter) {
  unsigned int textureID;
  glGenTextures(1, &textureID);

  int w, h, n;
  stbi_set_flip_vertically_on_load(true);
  unsigned char *data = NULL;

  // Search Strategy:
  // 1. Current dir
  // 2. textures/ (Standard project structure)
  // 3. resources/textures/
  // 4. ../../Lab_4/ (Developmental path for this specific repository)
  std::string paths[] = {std::string(fileName),
                         "textures/" + std::string(fileName),
                         "resources/textures/" + std::string(fileName),
                         "../../Lab_4/" + std::string(fileName)};

  std::string finalPath = "";
  for (const auto &p : paths) {
    data = stbi_load(p.c_str(), &w, &h, &n, 0);
    if (data) {
      finalPath = p;
      break;
    }
  }

  glBindTexture(GL_TEXTURE_2D, textureID);
  if (data) {
    std::cout << "[TEXTURE] Loaded: " << finalPath << " (" << w << "x" << h
              << ")\n";
    GLenum format = (n == 1) ? GL_RED : (n == 3 ? GL_RGB : GL_RGBA);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE,
                 data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    stbi_image_free(data);
  } else {
    std::cout << "[TEXTURE] Failed to load: " << fileName
              << " (Fallback to white)\n";
    unsigned char white[] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, minFilter);
  }
  return textureID;
}

// ALWAYS-VALID SOLID TEXTURE
unsigned int createSolidTextureRGBA(unsigned char r, unsigned char g,
                                    unsigned char b, unsigned char a) {
  unsigned int tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);

  unsigned char px[4] = {r, g, b, a};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               px);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapModes[currentWrap]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapModes[currentWrap]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  filterModes[currentFilter]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  filterModes[currentFilter]);

  return tex;
}