#version 330 core
#define Coprimes vec2(2, 3)

layout (location = 0) out vec4 o_Color;
in vec2 v_TexCoords;

uniform float u_Time;

uniform samplerCube u_Skymap;

uniform mat4 u_ViewProjection;
uniform mat4 u_Projection;
uniform mat4 u_View;
uniform mat4 u_InverseProjection;
uniform mat4 u_InverseView;

uniform float u_Width;
uniform float u_Height;

const float G = 6.67430e-11;  
const float c = 2.99792458e8;        
const float M = 2.5e10;
const float Cd =  1.00000e16; // cutoff distance

float Schwarzschild(float mass) {
    return 2.0 * G * mass / (c * c);
}

float SDF(vec3 P, vec3 S){
	return length(P - S);
}

vec3 SampleSpace(vec3 D) {
    vec3 C = texture(u_Skymap, D).xyz;
    float Sat = distance(C,vec3(1.));
    float Multiplier = 1.;

    Multiplier = clamp(1.6 * sin(u_Time / 1.4) * sin(u_Time /1.4), 0.9f, 100.0f);
	Multiplier = mix(Multiplier, 1.0f, smoothstep(1.7, 1.717, Sat));
    return Multiplier * texture(u_Skymap, D).xyz * 34.;
}

float Force(float mass, float r){
	return  G * (mass / (r * r));
}

float Schwarzchild(const float mass){
	return 2.0 * G * mass;
}

vec3 March(vec3 RayOrigin, vec3 RayDirection, out float Multiplier){
	
    vec3 BlackHole = vec3(0.0f,0.0f,10.) - RayOrigin;

	const float Steps = 127;

    vec3 Position = RayDirection;
    
    Multiplier = 1.;
    float sRadius = Schwarzchild(M);
    
    float Traversal = 0.0;
    
    for (int i = 0; i < Steps; ++i){
		float m = SDF(Position, BlackHole);
        Traversal += m;
        
        vec3 ToSingularity = normalize(Position - BlackHole);

        RayDirection -= ToSingularity * Force(M, m);
        RayDirection = normalize(RayDirection);
        
        if (m < sRadius) {
            Multiplier = 0.0f; 
            break;
        }

        if (Traversal > Cd) {
            break;
        }

        Position += RayDirection * (m - (sRadius * 0.9999944444555));
    }
    
    return Position;
}

vec3 SamplePixel(vec3 RayOrigin, vec3 RayDirection){
    float L = 1.0f;
    return SampleSpace(normalize(March(RayOrigin, RayDirection, L))) * L;
}

// Halton sampling pattern
vec2 Halton(vec2 index) {
    vec4 result = vec4(1, 1, 0, 0);

    while (index.x > 0.0 && index.y > 0.0) {
        result.xy /= Coprimes;
        result.zw += result.xy * mod(index, Coprimes);
        index = floor(index / Coprimes);
    }

    return result.zw;
}


void main() {

    // Perform supersampling
    int SuperSamplingSteps = 8;
    vec2 TexelSize = 1. / vec2(u_Width, u_Height);
    vec3 Color = vec3(0.0f);

    float Weight = 0.0f;
    
    for (int i = 0 ; i < SuperSamplingSteps ; i++) {
        vec2 Offset = Halton(vec2(i+1)) - 0.5f;
        vec2 TexCoords = v_TexCoords + Offset * TexelSize;

        vec4 Clip = vec4(TexCoords * 2.0f - 1.0f, -1.0, 1.0);
        vec4 Eye = vec4(vec2(u_InverseProjection * Clip), -1.0, 0.0);
        vec3 RayDirection = normalize(vec3(u_InverseView * Eye));
        vec3 RayOrigin = u_InverseView[3].xyz;

        Color += SamplePixel(RayOrigin, RayDirection);
        Weight += 1.0f;
    }

    Color /= Weight;
    Color = 1.0 - exp(-Color);
    o_Color = vec4(Color, 1.);
}