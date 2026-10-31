#version 330 core

in vec3 vertexPos;
in vec3 vertexNormal;

out vec4 fragColor;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor = vec3(1.0, 1.0, 1.0);

// https://learnopengl.com/Advanced-Lighting/Advanced-Lighting

void main() {
    float ambientStrength = 0.05;
    vec3 ambient = ambientStrength * lightColor;

    vec3 lightDir = normalize(lightPos - vertexPos);
    vec3 normal = normalize(vertexNormal);
    float diff = max(dot(lightDir, normal), 0.0);
    vec3 diffuse = diff * lightColor;

    float specularStrength = 0.3;
    vec3 viewDir = normalize(viewPos - vertexPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = diff * pow(max(dot(normal, halfwayDir), 0.0), 32.0);
    vec3 specular = specularStrength * spec * lightColor;

    fragColor = vec4(ambient + diffuse + specular, 1.0);
}
