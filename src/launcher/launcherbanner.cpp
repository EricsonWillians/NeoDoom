
#include "launcherbanner.h"
#include <algorithm>
#include <zwidget/widgets/imagebox/imagebox.h>
#include <zwidget/core/image.h>

namespace
{
constexpr double DefaultLauncherWidth = 615.0;
constexpr double DefaultLauncherHeight = 760.0;
constexpr double BannerHeightFallbackAspect = 16.0 / 9.0;
constexpr double BannerWidthUtilization = 0.985;
constexpr double MinBannerHeight = 140.0;
constexpr double MaxBannerHeight = 360.0;
constexpr double BannerHeightFloorFraction = 0.14;
constexpr double BannerHeightCeilFraction = 0.38;
constexpr double BannerPreferredHeightFraction = 0.24;
constexpr double BannerHeightWideFloorFraction = 0.22;
constexpr double BannerWideAspectThreshold = 2.5;
constexpr double BannerTallAspectThreshold = 1.0 / 1.4;
}

LauncherBanner::LauncherBanner(Widget* parent) : Widget(parent)
{
	Logo = new ImageBox(this);
	BannerImage = Image::LoadResource("widgets/banner.png");
	Logo->SetImage(BannerImage);
	Logo->SetImageMode(ImageBoxMode::Contain);
}

double LauncherBanner::GetPreferredHeight() const
{
	const double width = GetWidth() > 0.0 ? GetWidth() : DefaultLauncherWidth;
	const double height = GetHeight() > 0.0 ? GetHeight() : DefaultLauncherHeight;
	return GetBannerHeight(width, height);
}

double LauncherBanner::GetBannerHeight(double width, double availableHeight) const
{
	const double safeHeight = std::max(1.0, availableHeight);
	const double preferredHeight = safeHeight * BannerPreferredHeightFraction;
	const double bannerAwareMinHeight = std::max({ MinBannerHeight, safeHeight * BannerHeightFloorFraction, preferredHeight * 0.82 });
	double minHeight = bannerAwareMinHeight;
	double maxHeight = std::max(minHeight, std::min(MaxBannerHeight, safeHeight * BannerHeightCeilFraction));

	if (BannerImage)
	{
		double bannerWidth = static_cast<double>(BannerImage->GetWidth());
		double bannerHeight = static_cast<double>(BannerImage->GetHeight());
		if (bannerWidth > 0.0 && bannerHeight > 0.0)
		{
			double contentWidth = std::clamp(width * BannerWidthUtilization, 1.0, width);
			double aspect = std::clamp(bannerWidth / bannerHeight, BannerTallAspectThreshold, 10.0);

			if (aspect >= BannerWideAspectThreshold)
			{
				minHeight = std::max(bannerAwareMinHeight, safeHeight * BannerHeightWideFloorFraction);
				maxHeight = std::max(minHeight, std::min(MaxBannerHeight, safeHeight * BannerHeightCeilFraction));
			}
			else if (aspect <= BannerTallAspectThreshold)
			{
				minHeight = std::max(bannerAwareMinHeight, safeHeight * (BannerHeightFloorFraction + 0.03));
				maxHeight = std::max(minHeight, std::min(MaxBannerHeight, safeHeight * BannerHeightCeilFraction));
			}
			else
			{
				// Keep a balanced default for portrait and square graphics.
				minHeight = std::max(bannerAwareMinHeight, safeHeight * BannerHeightFloorFraction);
				maxHeight = std::max(minHeight, std::min(MaxBannerHeight, safeHeight * BannerHeightCeilFraction));
			}

			return std::clamp(contentWidth / aspect, minHeight, maxHeight);
		}
	}

	return std::clamp(width / BannerHeightFallbackAspect, minHeight, maxHeight);
}

void LauncherBanner::OnGeometryChanged()
{
	const double width = std::max(1.0, GetWidth());
	const double height = GetBannerHeight(width, std::max(1.0, GetHeight()));
	const double bannerAreaWidth = std::clamp(width * BannerWidthUtilization, 1.0, width);
	const double bannerX = (width - bannerAreaWidth) * 0.5;

	// Keep the banner visually centered and avoid edge-to-edge stretching on unusual assets.
	Logo->SetFrameGeometry(
		bannerX,
		0.0,
		std::max(1.0, bannerAreaWidth),
		height
	);
}
