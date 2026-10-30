/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/labels.h"

namespace HistoryView::Controls {

class CharactersLimitLabel final : public Ui::FlatLabel {
public:
	CharactersLimitLabel(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> widgetToAlign,
		style::align align,
		QMargins margins = {});

	void setLeft(int value);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	int _lastValue = 0;
	bool _pill = false;
	not_null<Ui::RpWidget*> _widgetToAlign;
	Fn<void(int, const QRect &)> _position;

};

} // namespace HistoryView::Controls
