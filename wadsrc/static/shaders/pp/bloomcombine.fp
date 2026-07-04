
layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D Bloom;

void main()
{
	vec3 bloom = max(texture(Bloom, TexCoord).rgb, vec3(0.0));
	bloom = bloom / (vec3(1.0) + bloom * 0.35);
	FragColor = vec4(bloom, 0.0);
}
