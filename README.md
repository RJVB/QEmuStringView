# QStringView emulation for Qt 5.9 and possibly earlier

## Usage

Include, in this order:

#include <QString>
// #define QSTRINGVIEW_EMULATE
#include "qemustringview.h"

This will define a QEmuStringView class that implements a QString-based alternative for QStringView.
When QSTRINGVIEW_EMULATE is defined before the header is included, macros will be defined that replace QStringView with QEmuStringView.

This allows to use the QStringView class in Qt 5.9 and possibly earlier. The emulation also works in Qt 5.10 and later.

Tested with the (included and minimally adapted) tst_qstringview.cpp unittest from Qt 5.10, against stock Qt 5.9.8 and Qt 5.12.3 .

