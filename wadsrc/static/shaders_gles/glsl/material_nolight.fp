
vec3 ProcessMaterialLight(Material material, vec3 color)
{
	return material.Base.rgb * clamp(color + desaturate(uDynLightColor).rgb + vec3(uGIAmbientStrength), 0.0, 1.4);
}
