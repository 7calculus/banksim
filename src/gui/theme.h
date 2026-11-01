// theme.h - colours, status colour mapping and the application style sheet.
#pragma once

#include <QColor>
#include <QString>

#include "types.h"

class QApplication;

namespace theme {

// Semantic colours required by the PRD:
//   green = success, red = failure / race condition, yellow = waiting, blue = executing
inline const QColor kGreen{0x16, 0xa3, 0x4a};
inline const QColor kRed{0xdc, 0x26, 0x26};
inline const QColor kYellow{0xb4, 0x6a, 0x00};  // dark amber, readable as text
inline const QColor kBlue{0x25, 0x63, 0xeb};
inline const QColor kGrey{0x64, 0x74, 0x8b};

inline const QColor kGreenBg{0xdc, 0xfc, 0xe7};
inline const QColor kRedBg{0xfe, 0xe2, 0xe2};
inline const QColor kYellowBg{0xfe, 0xf3, 0xc7};
inline const QColor kBlueBg{0xdb, 0xea, 0xfe};
inline const QColor kGreyBg{0xf1, 0xf5, 0xf9};

void apply(QApplication& app);

QColor stateColor(banksim::ThreadState s);
QColor stateBg(banksim::ThreadState s);
QColor statusColor(banksim::TxStatus s);
QColor statusBg(banksim::TxStatus s);

// "#rrggbb" for use inside style sheets
QString css(const QColor& c);

}  // namespace theme
