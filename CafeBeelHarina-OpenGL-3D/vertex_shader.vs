#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;     
layout (location = 2) in vec2 aTexCoord;

out vec2 TexCoord;
out vec3 FragPos;
out vec4 VertexColor;   // ✅ REQUIRED (matches fragment shader)

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform bool isWater;
uniform float time;

// ✅ texture assignment controls
uniform vec4 baseColor;
uniform bool uUseTexture;
uniform bool uBlendWithColor;
uniform int  uComputeMode;    // 0 = vertex compute, 1 = fragment compute
uniform sampler2D uTex0;

void main()
{
    vec3 pos = aPos;

    // Water displacement
    if (isWater)
    {
        pos.y += 0.05 * sin(2.0 * pos.x + 2.0 * time)
              + 0.05 * sin(2.0 * pos.z + 1.5 * time);
    }

    vec4 worldPos = model * vec4(pos, 1.0);
    FragPos = worldPos.xyz;
    TexCoord = aTexCoord;

    // Default
    VertexColor = baseColor;

    // ✅ If mode is vertex-compute, compute final color here
    if (uComputeMode == 0)
    {
        vec4 texC = vec4(1.0);

        if (uUseTexture)
            texC = textureLod(uTex0, TexCoord, 0.0);

        if (!uUseTexture)
            VertexColor = baseColor;
        else if (uBlendWithColor)
            VertexColor = texC * baseColor;
        else
            VertexColor = texC;
    }

    gl_Position = projection * view * worldPos;
}