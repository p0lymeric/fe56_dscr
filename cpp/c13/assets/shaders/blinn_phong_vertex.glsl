#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in mat4 aM; // locations 2-5
layout (location = 6) in vec4 aColor;

out vec3 vertexPos;
out vec3 vertexNormal;
out vec4 vertexColor;

uniform mat4 vp;

void main() {
    vec4 worldPos = aM * vec4(aPos, 1.0);
    gl_Position = vp * worldPos;

    vertexPos = worldPos.xyz;
    vertexNormal = mat3(aM) * aNormal;
    vertexColor = aColor;
}
