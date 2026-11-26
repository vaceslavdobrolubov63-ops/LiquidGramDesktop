#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_localPos;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D Sampler0;

layout(std140, binding = 0) uniform LiquidGlassParams {
	vec4 rectSizeAndDpr;
	vec4 glassParams;
	vec4 tintColor;
	vec4 lightAndFlags;
};

const float PI = 3.14159265359;
const float GOLDEN_ANGLE = 2.39996323;
const int SAMPLES = 24;

float getNoise(vec2 uv) {
	vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(magic.z * fract(dot(uv, magic.xy)));
}

float iosSDF(vec2 p, vec2 size, float radius) {
	float halfMinSide = max(min(size.x, size.y) * 0.5, 1.0);
	float r = clamp(radius, 0.0, halfMinSide);
	float t = r / halfMinSide;
	float exponent = mix(5.0, 2.2, t * t);
	vec2 q = abs(p) - size * 0.5 + r;
	vec2 qPos = max(q, 0.0);
	float dist = pow(pow(qPos.x, exponent) + pow(qPos.y, exponent), 1.0 / exponent);
	return dist + min(max(q.x, q.y), 0.0) - r;
}

vec3 sampleScreen(vec2 uv) {
	return texture(Sampler0, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
}

vec3 getPleasantBlur(vec2 uv, vec2 onePixel, float radius, float noiseVal) {
	if (radius < 0.5) {
		return sampleScreen(uv);
	}

	vec3 acc = sampleScreen(uv) * 0.08;
	float totalWeight = 0.08;
	float pixelRadius = radius * 10.0;
	float baseAngle = noiseVal * 2.0 * PI;
	float cosA = cos(baseAngle);
	float sinA = sin(baseAngle);
	mat2 rot = mat2(cosA, -sinA, sinA, cosA);

	float fSamples = float(SAMPLES);
	for (int i = 0; i < SAMPLES; i++) {
		float fi = float(i);
		float r = sqrt((fi + 0.5) / fSamples) * 0.85;
		float theta = fi * GOLDEN_ANGLE;
		float rJitter = clamp(
			r + (getNoise(vec2(fi, noiseVal)) - 0.5) * 0.35 / fSamples,
			0.0,
			1.0);
		float angleJitter = (getNoise(vec2(fi + 100.0, noiseVal + 0.3)) - 0.5) * 0.08;
		vec2 dir = rot * vec2(cos(theta + angleJitter), sin(theta + angleJitter));
		vec2 off = dir * rJitter * pixelRadius * onePixel;
		float w = exp(-rJitter * rJitter * 1.3);
		acc += sampleScreen(uv + off) * w;
		totalWeight += w;
	}

	vec3 result = acc / totalWeight;
	result = (result - 0.5) * 1.04 + 0.5;
	result = mix(result, vec3(1.0), 0.04);
	return clamp(result, 0.0, 1.0);
}

void main() {
	vec2 rectSize = max(rectSizeAndDpr.xy, vec2(1.0));
	vec2 texSize = max(rectSizeAndDpr.zw, vec2(1.0));
	vec2 onePixel = 1.0 / texSize;

	float radius = glassParams.x;
	float blurStrength = glassParams.y;
	float distortionStrength = glassParams.z;
	float opacity = glassParams.w;

	vec2 p = v_localPos - rectSize * 0.5;
	float dist = iosSDF(p, rectSize, radius);

	float aa = 1.25;
	float shapeAlpha = 1.0 - smoothstep(-aa * 0.6, aa * 0.6, dist);
	float globalAlpha = shapeAlpha * opacity;
	if (globalAlpha <= 0.001) {
		discard;
	}

	float eps = 0.25;
	vec2 grad = vec2(
		iosSDF(p + vec2(eps, 0.0), rectSize, radius) - dist,
		iosSDF(p + vec2(0.0, eps), rectSize, radius) - dist
	);
	vec2 normal = normalize(grad + vec2(1e-5));

	float minSide = min(rectSize.x, rectSize.y);
	float distFromEdge = clamp(-dist / (minSide * 0.5), 0.0, 1.0);
	float distFromCenter = 1.0 - distFromEdge;
	float lensCurve = 1.0 - sqrt(max(0.0, 1.0 - distFromCenter * distFromCenter));

	vec2 pixelOffset = normal * lensCurve * 18.0 * distortionStrength;
	vec2 uvOffset = pixelOffset / rectSize;
	vec2 targetUV = v_uv - uvOffset;

	float currentBlur = blurStrength * (4.5 + 2.5 * distFromCenter);
	float noise = getNoise(v_localPos);
	vec3 glassColor = getPleasantBlur(targetUV, onePixel, currentBlur, noise);

	vec2 chromOffset = uvOffset * 0.12;
	float rimDispersion = smoothstep(0.35, 1.0, distFromCenter) * clamp(distortionStrength, 0.0, 1.5);
	if (rimDispersion > 0.01) {
		float rSample = sampleScreen(targetUV - chromOffset).r;
		float bSample = sampleScreen(targetUV + chromOffset).b;
		glassColor.r = mix(glassColor.r, rSample, rimDispersion * 0.45);
		glassColor.b = mix(glassColor.b, bSample, rimDispersion * 0.45);
	}

	glassColor = mix(glassColor, glassColor * tintColor.rgb, tintColor.a);
	glassColor *= 0.94;
	glassColor = mix(glassColor, vec3(0.95), 0.02);

	float borderThickness = 1.3;
	float borderMask = smoothstep(-borderThickness - 1.2, -0.5, dist)
		* (1.0 - smoothstep(-0.7, -0.05, dist));

	vec2 viewDir = normalize(lightAndFlags.xy + vec2(1e-5));
	float incidence = dot(normal, viewDir);
	float softGlint = pow(max(0.0, incidence), 2.5) * 0.16;
	float sharpGlint = pow(max(0.0, incidence), 14.0) * 0.24;
	float glint1 = softGlint + sharpGlint;
	float glint2 = pow(max(0.0, -incidence), 4.0) * 0.10;
	float baseBorder = 0.09;
	vec3 borderColor = vec3(1.0) * (baseBorder + glint1 + glint2);

	vec3 finalColor = clamp(glassColor + (borderColor * borderMask), 0.0, 1.0);
	fragColor = vec4(finalColor * globalAlpha, globalAlpha);
}
