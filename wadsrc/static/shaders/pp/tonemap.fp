
layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D InputTexture;

vec3 Linear(vec3 c)
{
	//c = max(c, vec3(0.0));
	//return pow(c, 2.2);
	return c * c; // cheaper, but assuming gamma of 2.0 instead of 2.2
}

vec3 sRGB(vec3 c)
{
	c = max(c, vec3(0.0));
	//return pow(c, vec3(1.0 / 2.2));
	return sqrt(c); // cheaper, but assuming gamma of 2.0 instead of 2.2
}

vec3 ApplySaturation(vec3 color, float saturation)
{
	float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
	return mix(vec3(luma), color, saturation);
}

vec3 SoftClip(vec3 color, float shoulder)
{
	return color / (vec3(shoulder) + color);
}

vec3 ACESFilm(vec3 x)
{
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

#if defined(LINEAR)

vec3 Tonemap(vec3 color)
{
	return sRGB(color);
}

#elif defined(REINHARD)

vec3 Tonemap(vec3 color)
{
	color = color / (1 + color);
	return sRGB(color);
}

#elif defined(HEJLDAWSON)

vec3 Tonemap(vec3 color)
{
	vec3 x = max(vec3(0), color - 0.004);
	return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06); // no sRGB needed
}

#elif defined(UNCHARTED2)

vec3 Uncharted2Tonemap(vec3 x)
{
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Tonemap(vec3 color)
{
	float W = 11.2;
	vec3 curr = Uncharted2Tonemap(color);
	vec3 whiteScale = vec3(1) / Uncharted2Tonemap(vec3(W));
	return sRGB(curr * whiteScale);
}

#elif defined(PALETTE)

layout(binding=1) uniform sampler2D PaletteLUT;

vec3 Tonemap(vec3 color)
{
	ivec3 c = ivec3(clamp(color.rgb, vec3(0.0), vec3(1.0)) * 63.0 + 0.5);
	int index = (c.r * 64 + c.g) * 64 + c.b;
	int tx = index % 512;
	int ty = index / 512;
	return texelFetch(PaletteLUT, ivec2(tx, ty), 0).rgb;
}

#elif defined(GOTHIC)

vec3 Tonemap(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 mapped = SoftClip(color * vec3(0.90, 0.88, 0.96), 0.82);
	mapped = pow(mapped, vec3(1.16, 1.20, 1.08));
	mapped = ApplySaturation(mapped, 0.68);
	mapped *= vec3(0.83, 0.88, 1.06);
	mapped += vec3(0.012, 0.016, 0.028);
	return sRGB(clamp(mapped, 0.0, 1.0));
}

#elif defined(GOTHIC_NOIR)

vec3 Tonemap(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 mapped = SoftClip(color * 0.92, 0.74);
	float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
	luma = pow(clamp(luma, 0.0, 1.0), 1.34);
	vec3 noir = vec3(luma) * vec3(0.86, 0.91, 1.02);
	noir += smoothstep(0.62, 1.0, mapped) * vec3(0.02, 0.025, 0.045);
	return sRGB(clamp(noir, 0.0, 1.0));
}

#elif defined(MOONLIT)

vec3 Tonemap(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 mapped = SoftClip(color * vec3(0.72, 0.82, 1.18), 0.88);
	float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
	vec3 night = mix(vec3(luma) * vec3(0.54, 0.64, 0.98), mapped * vec3(0.72, 0.82, 1.12), 0.52);
	night = pow(clamp(night, 0.0, 1.0), vec3(1.08, 1.06, 0.96));
	night += vec3(0.006, 0.012, 0.035);
	return sRGB(clamp(night, 0.0, 1.0));
}

#elif defined(CANDLELIT)

vec3 Tonemap(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 mapped = SoftClip(color * vec3(1.18, 0.90, 0.62), 0.86);
	float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
	vec3 warm = mix(vec3(luma) * vec3(1.28, 0.72, 0.42), mapped, 0.62);
	warm = pow(clamp(warm, 0.0, 1.0), vec3(0.98, 1.12, 1.30));
	warm *= vec3(1.08, 0.92, 0.72);
	return sRGB(clamp(warm, 0.0, 1.0));
}

#elif defined(GRAVEYARD)

vec3 Tonemap(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 mapped = SoftClip(color * vec3(0.72, 0.92, 0.86), 0.80);
	float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
	vec3 cold = mix(vec3(luma) * vec3(0.48, 0.76, 0.66), mapped * vec3(0.66, 0.98, 0.86), 0.45);
	cold = ApplySaturation(cold, 0.54);
	cold = pow(clamp(cold + vec3(0.0, 0.012, 0.008), 0.0, 1.0), vec3(1.22, 1.08, 1.14));
	return sRGB(clamp(cold, 0.0, 1.0));
}

#elif defined(SILENT_HILL)

vec3 Tonemap(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 mapped = ACESFilm(color * vec3(0.86, 0.90, 0.82));
	float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
	vec3 sick = mix(vec3(luma), mapped, 0.34) * vec3(0.92, 0.98, 0.84);
	sick += vec3(0.030, 0.036, 0.020);
	sick = pow(clamp(sick, 0.0, 1.0), vec3(1.26, 1.18, 1.34));
	sick = mix(sick, vec3(dot(sick, vec3(0.299, 0.587, 0.114))), 0.18);
	return sRGB(clamp(sick, 0.0, 1.0));
}

#elif defined(BLEACH_BYPASS)

vec3 Tonemap(vec3 color)
{
	color = ACESFilm(max(color, vec3(0.0)) * 1.08);
	float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
	vec3 silver = mix(vec3(luma), color, 0.38);
	vec3 contrast = (silver - 0.5) * 1.32 + 0.5;
	vec3 retained = mix(color, contrast, 0.72);
	return sRGB(clamp(retained * vec3(1.04, 1.02, 0.96), 0.0, 1.0));
}

#elif defined(LOTTES)

vec3 Tonemap(vec3 color)
{
	color = max(color, vec3(0.0));
	const vec3 a = vec3(1.6);
	const vec3 d = vec3(0.977);
	const vec3 hdrMax = vec3(8.0);
	const vec3 midIn = vec3(0.18);
	const vec3 midOut = vec3(0.267);
	vec3 b = (-pow(midIn, a) + pow(hdrMax, a) * midOut) / ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
	vec3 c = (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) / ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
	vec3 mapped = pow(color, a) / (pow(color, a * d) * b + c);
	return sRGB(clamp(mapped, 0.0, 1.0));
}

#elif defined(ACES)

vec3 Tonemap(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 mapped = ACESFilm(color);
	mapped = pow(mapped, vec3(0.96));
	return sRGB(clamp(mapped, 0.0, 1.0));
}

#else
#error Tonemap mode define is missing
#endif




void main()
{
	vec3 color = texture(InputTexture, TexCoord).rgb;
#ifndef PALETTE
	color = Linear(color); // needed because gzdoom's scene texture is not linear at the moment
#endif
	vec3 mapped = Tonemap(color);

	FragColor = vec4(mapped, 1.0);
}
