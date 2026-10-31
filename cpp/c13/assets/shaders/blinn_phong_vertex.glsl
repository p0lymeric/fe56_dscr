#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

out vec3 vertexPos;
out vec3 vertexNormal;

uniform mat4 vp;

void main() {
    gl_Position = vp * vec4(aPos, 1.0);

    vertexPos = aPos;
    vertexNormal = aNormal;
}
