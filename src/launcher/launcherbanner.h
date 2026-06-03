#pragma once

#include <memory>

#include <zwidget/core/image.h>

#include <zwidget/core/widget.h>

class ImageBox;
class TextLabel;

class LauncherBanner : public Widget
{
public:
	LauncherBanner(Widget* parent);

	double GetPreferredHeight() const;

private:
	double GetBannerHeight(double width, double availableHeight) const;
	void OnGeometryChanged() override;
	std::shared_ptr<Image> BannerImage;

	ImageBox* Logo = nullptr;
};
