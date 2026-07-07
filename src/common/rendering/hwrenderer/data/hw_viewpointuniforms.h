#pragma once

#include "matrix.h"

struct HWDrawInfo;

enum class ELightBlendMode : uint8_t
{
	CLAMP = 0,
	CLAMP_COLOR = 1,
	NOCLAMP = 2,

	DEFAULT = CLAMP,
};

struct HWViewpointUniforms
{
	VSMatrix mProjectionMatrix;
	VSMatrix mViewMatrix;
	VSMatrix mNormalViewMatrix;
	FVector4 mCameraPos;
	FVector4 mClipLine;

	float mGlobVis = 1.f;
	int mPalLightLevels = 0;
	int mViewHeight = 0;
	float mClipHeight = 0.f;
	float mClipHeightDirection = 0.f;
	int mShadowmapFilter = 1;

	int mLightBlendMode = 0;

	float mThickFogDistance = -1.f;
	float mThickFogMultiplier = 30.f;
	int mDynLightFalloffMode = 0;
	float mDynLightFalloffExponent = 2.f;
	float mDynLightIntensity = 1.f;
	float mDynLightSaturation = 1.f;
	float mLightTemperature = 0.f;
	float mLightAmbientFloor = 0.f;
	float mLightSpecularScale = 1.f;
	float mDynLightRangeScale = 1.f;
	float mDynLightFalloffSoftness = 0.f;
	float mDynLightWrap = 0.f;
	float mDynLightIndirect = 0.f;
	float mDynLightShadowStrength = 1.f;
	float mEmissiveBoost = 0.f;
	float mGIAmbientStrength = 0.f;
	float mLightStylePadding = 0.f;
	FVector4 mFogGradientColor = { 0.f, 0.f, 0.f, 0.f };
	FVector4 mFogGradientDirection = { 0.f, 1.f, 0.f, 0.f };

	void CalcDependencies()
	{
		mNormalViewMatrix.computeNormalMatrix(mViewMatrix);
	}
};
