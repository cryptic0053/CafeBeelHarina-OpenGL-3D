#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 FragPos;
in vec4 VertexColor;   // used when uComputeMode == 0

uniform vec4 baseColor;
uniform bool isWater;
uniform bool isDeck;
uniform bool isSky;
uniform float time;

// ---- texture assignment controls ----
uniform bool uUseTexture;        // ON/OFF texture
uniform bool uBlendWithColor;    // multiply texture with baseColor
uniform int  uComputeMode;       // 0 = vertex computed, 1 = fragment computed
uniform sampler2D uTex0;

void main()
{
    float dist = abs(FragPos.z);

    // -------------------------
    // 1) SKY (unchanged style)
    // -------------------------
    if (isSky)
    {
        float h = FragPos.y;
        vec3 skyTop    = vec3(0.65, 0.82, 1.0);
        vec3 skyBottom = vec3(0.45, 0.65, 0.90);
        vec3 result = mix(skyBottom, skyTop, clamp((h - 5.0) / 50.0, 0.0, 1.0));
        FragColor = vec4(result, 1.0);
        return;
    }

    // -------------------------
    // 2) WATER (unchanged style)
    // -------------------------
    if (isWater)
    {
        vec3 result = baseColor.rgb;

        float wave = sin(FragPos.z * 1.8 + time * 1.5) * 0.5 + 0.5;
        result = mix(result, result * 1.12, wave * 0.15);

        if (FragPos.z < -45.0) {
            float horizonFactor = clamp((abs(FragPos.z) - 45.0) / 30.0, 0.0, 1.0);
            result = mix(result, vec3(0.55, 0.75, 0.95), horizonFactor * 0.5);
        }

        // horizon highlight
        if (abs(FragPos.z) > 74.5) {
            result = mix(result, vec3(0.5, 0.7, 1.0), 0.9);
        }

        FragColor = vec4(result, baseColor.a);
        return;
    }

    // =========================================================
    // 3) NORMAL OBJECTS (Texture Mapping Assignment)
    // =========================================================

    vec4 finalCol;

    // A) Vertex computed mode (color already computed in vertex shader)
    if (uComputeMode == 0)
    {
        finalCol = VertexColor;
    }
    // B) Fragment computed mode
    else
    {
        vec4 texC = vec4(1.0);
        if (uUseTexture)
            texC = texture(uTex0, TexCoord);

        if (!uUseTexture)
            finalCol = baseColor;                          // no texture
        else if (uBlendWithColor)
            finalCol = texC * baseColor;                   // blended with surface color
        else
            finalCol = texC;                                // simple texture only
    }

    // -------------------------
    // 4) Object fog + distance darkening (keep your look)
    // -------------------------
    vec3 result = finalCol.rgb;

    float darkness = clamp((dist - 10.0) / 70.0, 0.0, 0.35);
    result *= (1.0 - darkness);

    if (dist > 55.0) {
        float fog = clamp((dist - 55.0) / 25.0, 0.0, 1.0);
        result = mix(result, vec3(0.48, 0.68, 0.92), fog * 0.7);
    }

    // horizon highlight for far objects
    if (abs(FragPos.z) > 74.5) {
        result = mix(result, vec3(0.5, 0.7, 1.0), 0.25);
    }

    FragColor = vec4(result, finalCol.a);
}