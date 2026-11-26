// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"

#include <QtCore/QPointer>
#include <QtGui/QColor>
#include <QtGui/QImage>

namespace Ui::GL {
class Surface;
} // namespace Ui::GL

namespace Ayu::Ui {

class LiquidGlassWidget : public ::Ui::RpWidget {
public:
	struct Style {
		float cornerRadius = 16.f;
		float blurStrength = 1.0f;
		float distortionStrength = 0.85f;
		float opacity = 1.0f;
		QColor tintColor = QColor(255, 255, 255);
		float tintStrength = 0.14f;
		QPointF lightDirection = QPointF(-0.55, -0.83);
	};

	explicit LiquidGlassWidget(QWidget *parent, Style style = {});
	~LiquidGlassWidget() override;

	[[nodiscard]] static bool Supported();

	void setStyle(Style style);
	void setCornerRadius(float radius);
	void setBlurStrength(float strength);
	void setDistortionStrength(float strength);
	void setOpacity(float opacity);
	void setTintColor(QColor color, float strength);
	void setLightDirection(QPointF dir);

	void setBackdropImage(QImage image);
	void setAutoCaptureParent(bool enabled);
	void setCaptureSource(QWidget *source);
	void refreshBackdrop();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void moveEvent(QMoveEvent *e) override;
	void showEvent(QShowEvent *e) override;

private:
	class Renderer;

	void ensureSurface();
	void updateSurfaceState();
	void captureFromParent();
	void scheduleCapture();
	[[nodiscard]] QWidget *surfaceWidget() const;

	Style _style;
	QImage _backdrop;
	QPointer<QWidget> _captureSource;
	bool _autoCaptureParent = false;
	bool _capturing = false;
	base::Timer _captureTimer;
	std::unique_ptr<::Ui::GL::Surface> _surface;
	Renderer *_renderer = nullptr;
};

} // namespace Ayu::Ui
