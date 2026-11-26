/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ayu::Ui {
class LiquidGlassWidget;
} // namespace Ayu::Ui

namespace Ui {

class JumpDownButton final : public RippleButton {
public:
	JumpDownButton(QWidget *parent, const style::TwoIconButton &st);
	~JumpDownButton() override;

	void setUnreadCount(int unreadCount);
	[[nodiscard]] int unreadCount() const {
		return _unreadCount;
	}

	void setLiquidGlass(bool enabled, QWidget *captureSource = nullptr);
	void refreshLiquidGlass();

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::TwoIconButton &_st;

	Ayu::Ui::LiquidGlassWidget *_glass = nullptr;
	int _unreadCount = 0;

};

} // namespace Ui
