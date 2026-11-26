#version 450

layout(location = 0) in vec2 position;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec2 v_localPos;

layout(std140, binding = 0) uniform LiquidGlassParams {
	vec4 rectSizeAndDpr;
	vec4 glassParams;
	vec4 tintColor;
	vec4 lightAndFlags;
};

void main() {
	v_uv = position;
	v_localPos = position * rectSizeAndDpr.xy;
	float flipY = lightAndFlags.z;
	vec2 ndc = vec2(position.x * 2.0 - 1.0, (1.0 - position.y * 2.0) * flipY);
	gl_Position = vec4(ndc, 0.0, 1.0);
}
