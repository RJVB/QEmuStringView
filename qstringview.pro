CONFIG += testcase
TARGET = tst_qstringview
## uncomment to enable QStringView emulation:
# DEFINES += QSTRINGVIEW_EMULATE
QT = core testlib
contains(QT_CONFIG, c++14):CONFIG *= c++14
contains(QT_CONFIG, c++1z):CONFIG *= c++1z
SOURCES += tst_qstringview.cpp
HEADERS += qemustringview.h
