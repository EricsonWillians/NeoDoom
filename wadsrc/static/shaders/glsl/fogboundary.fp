layout(location=2) in vec4 pixelpos;
layout(location=0) out vec4 FragColor;
#ifdef GBUFFER_PASS
layout(location=1) out vec4 FragFog;
layout(location=2) out vec4 FragNormal;
#endif

//===========================================================================
//
// Main shader routine
//
//===========================================================================

void main()
{
	float fogdist;
	float fogfactor;

	//
	// calculate fog factor
	//
	if (uFogEnabled == -1) 
	{
		fogdist = pixelpos.w;
	}
	else 
	{
		fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
	}
	// Match the enhanced fog of the main shader so boundary planes blend
	// seamlessly with the fogged geometry next to them: smooth thick-fog
	// distance shaping plus the analytic exponential height fog integral
	// (see getEnhancedFogFactor in main.fp).
	if (uThickFogDistance > 0.0 && fogdist > uThickFogDistance)
	{
		float excess = fogdist - uThickFogDistance;
		float transition = max(48.0, uThickFogDistance * 0.30);
		fogdist += uThickFogMultiplier * excess * smoothstep(0.0, transition, excess);
	}
	fogdist = max(fogdist, 16.0);
	float k = uFogQuality.y * (1.0 / 256.0);
	float x = clamp(k * (pixelpos.z - uCameraPos.z), -64.0, 64.0);
	float heightIntegral = (abs(x) < 0.001) ? 1.0 : (1.0 - exp2(-x)) / (x * 0.6931471805599453);
	fogfactor = exp2 (uFogDensity * fogdist * heightIntegral);
	// Same smooth visibility floor as the main shader so the boundary plane's
	// opacity converges with the fog on the geometry next to it.
	fogfactor = uFogMinVisibility + (1.0 - uFogMinVisibility) * fogfactor;
	FragColor = vec4(uFogColor.rgb, 1.0 - fogfactor);
#ifdef GBUFFER_PASS
	FragFog = vec4(0.0, 0.0, 0.0, 1.0);
	FragNormal = vec4(0.5, 0.5, 0.5, 1.0);
#endif
}

