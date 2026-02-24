#ifndef CYLINDER_H
#define CYLINDER_H

#include <glad/glad.h>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

class Cylinder {
public:
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    unsigned int vao, vbo, ebo;

    Cylinder(float baseRadius = 1.0f, float topRadius = 1.0f, float height = 1.0f, int sectorCount = 36, int stackCount = 1) {
        float x, y, z;
        float nx, ny, nz;
        float s, t;

        float sectorStep = 2 * glm::pi<float>() / sectorCount;
        float stackStep = height / stackCount;

        for (int i = 0; i <= stackCount; ++i) {
            float zPos = -height / 2.0f + i * stackStep;
            float radius = baseRadius + (float)i / stackCount * (topRadius - baseRadius);

            for (int j = 0; j <= sectorCount; ++j) {
                float sectorAngle = j * sectorStep;
                x = radius * cosf(sectorAngle);
                y = radius * sinf(sectorAngle);
                z = zPos;

                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);

                // Normal (approximate for cylinder/cone)
                float absN = sqrt(x*x + y*y);
                nx = (absN == 0) ? 0 : x / absN;
                ny = (absN == 0) ? 0 : y / absN;
                nz = 0; // Simple cylindrical normal
                vertices.push_back(nx);
                vertices.push_back(ny);
                vertices.push_back(nz);

                s = (float)j / sectorCount;
                t = (float)i / stackCount;
                vertices.push_back(s);
                vertices.push_back(t);
            }
        }

        // Indices
        for (int i = 0; i < stackCount; ++i) {
            int k1 = i * (sectorCount + 1);
            int k2 = k1 + sectorCount + 1;
            for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);

                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
        setupMesh();
    }

    void draw() {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, (unsigned int)indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

private:
    void setupMesh() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }
};
#endif
