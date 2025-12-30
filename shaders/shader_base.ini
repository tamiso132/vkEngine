#pragma once

#define BINDING_SAMPLED   0
#define BINDING_STORAGE_IMAGE    1
#define BINDING_STORAGE_BUFFER     2

#if defined(__STDC__)
    // --- C / C++ Sidan ---
    #include <stdint.h>
    
    // Mappa GLSL typer till C-typer
    typedef uint32_t uint;
    typedef float    vec2[2];
    typedef float    vec3[3];
    typedef float    vec4[4];
    typedef int      ivec2[2];
    
    
    // I C behöver vi inte layout(...) keywords, så vi macro:ar bort dem eller gör tomma
    // Men för structs måste vi vara försiktiga med alignment (se nedan)
    
#else
    // --- GLSL Sidan ---
#endif

// --- 3. Shared Structs ---

// Push Constants (Måste vara identisk i C och Shader)
// VIKTIGT: Använd 'alignas(16)' i C++ eller manuell padding i C för vec3/vec4
// För C (enkelhetens skull): Håll dig till scalar types (uint, float) eller vec4 (16 bytes).

struct PushData {
    uint inputTextureID;
    uint outputImageID;
    uint _pad0;          // Padding för att matcha alignment om det behövs
    uint _pad1;
    
    // Exempel på data
    float time; 
    float exposure;
    uint  flags;
    uint  mode;
};

// Exempel på en GPU-struct för en Buffer (std430)
struct SceneData {
    float viewMatrix[16]; // mat4
    float projMatrix[16]; // mat4
    float cameraPos[4];   // vec3 + 1 float padding (GLSL vec3 i UBO är 16 bytes!)
};

