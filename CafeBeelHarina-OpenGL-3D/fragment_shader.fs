#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 FragPos;
in vec4 VertexColor;   // used when uComputeMode == 0
in vec3 Normal;

uniform vec3 viewPos;
uniform bool masterLightOn;
uniform bool dirLightOn;
uniform bool pointLightOn;
uniform bool spotLightOn;
uniform bool ambientOn;
uniform bool diffuseOn;
uniform bool specularOn;
uniform bool emissiveOn;

uniform vec3 dirLightDir;
uniform vec3 spotLightDir;
uniform vec3 pointLightPositions[4];
uniform bool uIsEmissiveObject;

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

    
    // 1) SKY
    
    if (isSky)
    {
        float h = FragPos.y;
        vec3 skyTop    = vec3(0.65, 0.82, 1.0);
        vec3 skyBottom = vec3(0.45, 0.65, 0.90);
        vec3 result = mix(skyBottom, skyTop, clamp((h - 5.0) / 50.0, 0.0, 1.0));
        FragColor = vec4(result, 1.0);
        return;
    }

    
    // 2) WATER
    
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

    // 3) NORMAL OBJECTS

    vec4 finalCol;

    // A) Vertex computed mode
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

    
    // 4) Object fog + distance darkening
    
    vec3 result = finalCol.rgb;

    // 3.5) BLINN-PHONG LIGHTING ENGINE
    if (masterLightOn) 
    {
        vec3 norm = normalize(Normal);
        vec3 viewDir = normalize(viewPos - FragPos);
        vec3 lightingAcc = vec3(0.0);

        if (ambientOn) lightingAcc += 0.25 * finalCol.rgb;

        // A) Directional Light (The Sun / Moon)
        if (dirLightOn) {
            vec3 lightDir = normalize(-dirLightDir);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuseOutput = (diffuseOn) ? (diff * vec3(0.8) * finalCol.rgb) : vec3(0.0);
            
            vec3 halfwayDir = normalize(lightDir + viewDir);  
            float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
            vec3 specularOutput = (specularOn) ? (spec * vec3(0.2)) : vec3(0.0);
            
            lightingAcc += (diffuseOutput + specularOutput);
        }

        // B) Point Lights (Canopy Bulbs)
        if (pointLightOn) {
            for(int i = 0; i < 4; i++) {
                vec3 lightDir = normalize(pointLightPositions[i] - FragPos);
                float distance = length(pointLightPositions[i] - FragPos);
                float attenuation = 1.0 / (1.0 + 0.045 * distance + 0.0075 * (distance * distance));    
                
                float diff = max(dot(norm, lightDir), 0.0);
                vec3 diffuseOutput = (diffuseOn) ? (diff * vec3(0.9, 0.8, 0.4) * finalCol.rgb) : vec3(0.0);
                
                vec3 halfwayDir = normalize(lightDir + viewDir);  
                float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
                vec3 specularOutput = (specularOn) ? (spec * vec3(0.4, 0.35, 0.1)) : vec3(0.0);
                
                lightingAcc += (diffuseOutput + specularOutput) * attenuation;
            }
        }

        // C) Spot Light (Flashlight from Camera)
        if (spotLightOn) {
            vec3 lightDir = normalize(viewPos - FragPos); 
            float theta = dot(lightDir, normalize(-spotLightDir)); 
            
            if(theta > cos(radians(12.5))) 
            {
                float diff = max(dot(norm, lightDir), 0.0);
                vec3 diffuseOutput = (diffuseOn) ? (diff * vec3(1.0) * finalCol.rgb) : vec3(0.0);
                
                vec3 halfwayDir = normalize(lightDir + viewDir);  
                float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
                vec3 specularOutput = (specularOn) ? (spec * vec3(0.5)) : vec3(0.0);
                
                lightingAcc += (diffuseOutput + specularOutput);
            }
        }

        // D) Emissive
        if (emissiveOn && uIsEmissiveObject) {
            lightingAcc += finalCol.rgb * 0.4;
        }

        result = lightingAcc;
    }

    // 4) Object fog + preserve old distance styling if lights are off
    float darkness = clamp((dist - 10.0) / 70.0, 0.0, 0.35);
    if (!masterLightOn) result *= (1.0 - darkness);

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