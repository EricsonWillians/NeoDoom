
vec3 ProcessMaterialLight(Material material, vec3 color)
{
	return material.Base.rgb * clamp(ApplyBiasedAmbientFloor(color + desaturate(vec4(ApplyBiasedDynamicLight(uDynLightColor.rgb), uDynLightColor.a)).rgb + vec3(uGIAmbientStrength)), 0.0, 1.4);
}
