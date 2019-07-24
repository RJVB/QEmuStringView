/****************************************************************************
**
** Copyright (C) 2018 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Marc Mutz <marc.mutz@kdab.com>
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QString>

#include <QChar>
#include <QStringRef>

#include <QTest>
#include <QDebug>

#include <string>

#define QSTRINGVIEW_EMULATE

#ifdef __SSE2__
#include <immintrin.h>
#endif
#include <qalgorithms.h>
#include <qglobal.h>

#if !defined(Q_ASSERT10)
#  if defined(QT_NO_DEBUG) && !defined(QT_FORCE_ASSERTS)
#    define Q_ASSERT10(cond) static_cast<void>(false && (cond))
#  else
#    define Q_ASSERT10(cond) ((cond) ? static_cast<void>(0) : qt_assert(#cond, __FILE__, __LINE__))
#  endif
#endif

/*
  quintptr and qptrdiff is guaranteed to be the same size as a pointer, i.e.

      sizeof(void *) == sizeof(quintptr)
      && sizeof(void *) == sizeof(qptrdiff)
*/
template <int> struct QIntegerForSize;
typedef QIntegerForSize<Q_PROCESSOR_WORDSIZE>::Signed qregisterint;
typedef QIntegerForSize<Q_PROCESSOR_WORDSIZE>::Unsigned qregisteruint;
typedef QIntegerForSizeof<void*>::Unsigned quintptr;
typedef QIntegerForSizeof<void*>::Signed qptrdiff;
typedef qptrdiff qintptr;
using qsizetype = QIntegerForSizeof<std::size_t>::Signed;

namespace QEmuPrivate {
    template <typename Char>
    struct IsCompatibleCharTypeHelper
        : std::integral_constant<bool,
                                 std::is_same<Char, QChar>::value ||
                                 std::is_same<Char, ushort>::value ||
    #if defined(Q_COMPILER_UNICODE_STRINGS)
                                 std::is_same<Char, char16_t>::value ||
    #endif
                                 (std::is_same<Char, wchar_t>::value && sizeof(wchar_t) == sizeof(QChar))> {};
    template <typename Char>
    struct IsCompatibleCharType
        : IsCompatibleCharTypeHelper<typename std::remove_cv<typename std::remove_reference<Char>::type>::type> {};

    template <typename Array>
    struct IsCompatibleArrayHelper : std::false_type {};
    template <typename Char, size_t N>
    struct IsCompatibleArrayHelper<Char[N]>
        : IsCompatibleCharType<Char> {};
    template <typename Array>
    struct IsCompatibleArray
        : IsCompatibleArrayHelper<typename std::remove_cv<typename std::remove_reference<Array>::type>::type> {};

    template <typename Pointer>
    struct IsCompatiblePointerHelper : std::false_type {};
    template <typename Char>
    struct IsCompatiblePointerHelper<Char*>
        : IsCompatibleCharType<Char> {};
    template <typename Pointer>
    struct IsCompatiblePointer
        : IsCompatiblePointerHelper<typename std::remove_cv<typename std::remove_reference<Pointer>::type>::type> {};

    template <typename T>
    struct IsCompatibleStdBasicStringHelper : std::false_type {};
    template <typename Char, typename...Args>
    struct IsCompatibleStdBasicStringHelper<std::basic_string<Char, Args...> >
        : IsCompatibleCharType<Char> {};

    template <typename T>
    struct IsCompatibleStdBasicString
        : IsCompatibleStdBasicStringHelper<
            typename std::remove_cv<typename std::remove_reference<T>::type>::type
          > {};

    qsizetype qustrlen(const ushort *str) Q_DECL_NOTHROW
    {
#ifdef __SSE2__
        // find the 16-byte alignment immediately prior or equal to str
        quintptr misalignment = quintptr(str) & 0xf;
        Q_ASSERT((misalignment & 1) == 0);
        const ushort *ptr = str - (misalignment / 2);

        // load 16 bytes and see if we have a null
        // (aligned loads can never segfault)
        const __m128i zeroes = _mm_setzero_si128();
        __m128i data = _mm_load_si128(reinterpret_cast<const __m128i *>(ptr));
        __m128i comparison = _mm_cmpeq_epi16(data, zeroes);
        quint32 mask = _mm_movemask_epi8(comparison);

        // ignore the result prior to the beginning of str
        mask >>= misalignment;

        // Have we found something in the first block? Need to handle it now
        // because of the left shift above.
        if (mask)
            return qCountTrailingZeroBits(quint32(mask)) / 2;

        do {
            ptr += 8;
            data = _mm_load_si128(reinterpret_cast<const __m128i *>(ptr));

            comparison = _mm_cmpeq_epi16(data, zeroes);
            mask = _mm_movemask_epi8(comparison);
        } while (mask == 0);

        // found a null
        uint idx = qCountTrailingZeroBits(quint32(mask));
        return ptr - str + idx / 2;
#else
        qsizetype result = 0;

        if (sizeof(wchar_t) == sizeof(ushort))
            return wcslen(reinterpret_cast<const wchar_t *>(str));

        while (*str++)
            ++result;
        return result;
#endif
    }

} // namespace QEmuPrivate

class QEmuStringView : public QString
{
public:
    typedef QChar storage_type;
    typedef const QChar value_type;
    typedef std::ptrdiff_t difference_type;
    typedef qsizetype size_type;
    typedef value_type &reference;
    typedef value_type &const_reference;
    typedef value_type *pointer;
    typedef value_type *const_pointer;

    typedef pointer iterator;
    typedef const_pointer const_iterator;
    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

    template <typename Char>
    using if_compatible_char = typename std::enable_if<QEmuPrivate::IsCompatibleCharType<Char>::value, bool>::type;

    template <typename Array>
    using if_compatible_array = typename std::enable_if<QEmuPrivate::IsCompatibleArray<Array>::value, bool>::type;

    template <typename Pointer>
    using if_compatible_pointer = typename std::enable_if<QEmuPrivate::IsCompatiblePointer<Pointer>::value, bool>::type;

    template <typename T>
    using if_compatible_string = typename std::enable_if<QEmuPrivate::IsCompatibleStdBasicString<T>::value, bool>::type;

    template <typename T>
    using if_compatible_qstring_like = typename std::enable_if<std::is_same<T, QString>::value || std::is_same<T, QStringRef>::value, bool>::type;

    template <typename Char, size_t N>
    static Q_DECL_CONSTEXPR qsizetype lengthHelperArray(const Char (&)[N]) Q_DECL_NOTHROW
    {
        return qsizetype(N - 1);
    }

    template <typename Char>
    static qsizetype lengthHelperPointer(const Char *str) Q_DECL_NOTHROW
    {
        if (!str) {
            return 0;
        }
#if defined(Q_CC_GNU) && !defined(Q_CC_CLANG) && !defined(Q_CC_INTEL)
        if (__builtin_constant_p(*str)) {
            qsizetype result = 0;
            while (*str++)
                ++result;
            return result;
        }
#endif
        return QEmuPrivate::qustrlen(reinterpret_cast<const ushort *>(str));
    }
    static qsizetype lengthHelperPointer(const QChar *str) Q_DECL_NOTHROW
    {
        return str ? QEmuPrivate::qustrlen(reinterpret_cast<const ushort *>(str)) : 0;
    }

    template <typename Char>
    static const storage_type *castHelper(const Char *str) Q_DECL_NOTHROW
    { return reinterpret_cast<const storage_type*>(str); }
    static Q_DECL_CONSTEXPR const storage_type *castHelper(const storage_type *str) Q_DECL_NOTHROW
    { return str; }
    template <typename Char>
    static Q_DECL_CONSTEXPR const storage_type *castHelper(const std::basic_string<Char> str) Q_DECL_NOTHROW
    { return reinterpret_cast<const storage_type*>(str.data()); }

public:
    inline QEmuStringView()
        : QString() {}
    QEmuStringView(std::nullptr_t) Q_DECL_NOTHROW
        : QString() {}
    inline QEmuStringView(const QString &that)
    {
        *this = that;
    }
    inline QEmuStringView(const QString *that)
    {
        if (that) {
            *this = *that;
        } else {
            clear();
        }
    }

//     inline QEmuStringView(const QChar *str, qsizetype len=-1)
//         : QString(str,len) {}
    template <typename Char, if_compatible_char<Char> = true>
    inline Q_DECL_CONSTEXPR QEmuStringView(const Char *str, qsizetype len)
        : QString(castHelper(str),len) {}
    template <typename Char, if_compatible_char<Char> = true>
    inline Q_DECL_CONSTEXPR QEmuStringView(const Char *f, const Char *l)
        : QEmuStringView(f, l - f) {}
    template <typename Char, if_compatible_char<Char> = true>
    inline Q_DECL_CONSTEXPR QEmuStringView(const std::basic_string<Char> str)
        : QString(castHelper(str),-1) {}
    inline QEmuStringView(const std::wstring str)
        : QString(QString::fromStdWString(str)) {}
    inline QEmuStringView(const wchar_t *str)
        : QString(QString::fromStdWString(str)) {}
    template <typename Array, if_compatible_array<Array> = true>
    Q_DECL_CONSTEXPR QEmuStringView(const Array &str) Q_DECL_NOTHROW
        : QEmuStringView(str, lengthHelperArray(str)) {}

    template <typename Pointer, if_compatible_pointer<Pointer> = true>
    Q_DECL_CONSTEXPR QEmuStringView(const Pointer &str) Q_DECL_NOTHROW
        : QEmuStringView(str, str ? lengthHelperPointer(str) : 0) {}

    inline QEmuStringView(const QStringRef &that)
        : QEmuStringView(that.string())
    {}
    template <typename String, if_compatible_qstring_like<String> = true>
    inline Q_DECL_CONSTEXPR QEmuStringView(const String &str) Q_DECL_NOTHROW
        : QEmuStringView(str.isNull() ? nullptr : str.data(), qsizetype(str.size())) {}


    inline bool empty() {return QString::size() == 0 ; }
    Q_REQUIRED_RESULT inline QChar first() const { return at(0); }
    Q_REQUIRED_RESULT inline QChar front() const { return at(0); }
    Q_REQUIRED_RESULT inline QChar last()  const { return at(size() - 1); }
    Q_REQUIRED_RESULT inline QChar back()  const { return at(size() - 1); }

    inline QString toString() const
    {
        // detour via QStringRef::toString() which makes the required deep copy
        return QStringRef(this).toString();
    }

    char *toPrettyUnicode() const
    {
        auto p = reinterpret_cast<const ushort *>(utf16());
        auto length = size();
        // keep it simple for the vast majority of cases
        bool trimmed = false;
        QScopedArrayPointer<char> buffer(new char[256]);
        const ushort *end = p + length;
        char *dst = buffer.data();

        *dst++ = '"';
        for ( ; p != end; ++p) {
            if (dst - buffer.data() > 245) {
                // plus the the quote, the three dots and NUL, it's 250, 251 or 255
                trimmed = true;
                break;
            }

            if (*p < 0x7f && *p >= 0x20 && *p != '\\' && *p != '"') {
                *dst++ = *p;
                continue;
            }

            // write as an escape sequence
            // this means we may advance dst to buffer.data() + 246 or 250
            *dst++ = '\\';
            switch (*p) {
            case 0x22:
            case 0x5c:
                *dst++ = uchar(*p);
                break;
            case 0x8:
                *dst++ = 'b';
                break;
            case 0xc:
                *dst++ = 'f';
                break;
            case 0xa:
                *dst++ = 'n';
                break;
            case 0xd:
                *dst++ = 'r';
                break;
            case 0x9:
                *dst++ = 't';
                break;
            default:
                *dst++ = 'u';
                *dst++ = toHexUpper(*p >> 12);
                *dst++ = toHexUpper(*p >> 8);
                *dst++ = toHexUpper(*p >> 4);
                *dst++ = toHexUpper(*p);
            }
        }

        *dst++ = '"';
        if (trimmed) {
            *dst++ = '.';
            *dst++ = '.';
            *dst++ = '.';
        }
        *dst++ = '\0';
        return buffer.take();
    }

    inline QEmuStringView &operator=(const QString &other) Q_DECL_NOTHROW
    {
        *(static_cast<QString*>(this)) = other;
        return *this;
    }
    inline QEmuStringView &operator=(const QString *other) Q_DECL_NOTHROW
    {
        *(static_cast<QString*>(this)) = *other;
        return *this;
    }
    inline QEmuStringView &operator=(const QStringRef &other) Q_DECL_NOTHROW
    {
        *this = other.string();
        return *this;
    }
    Q_REQUIRED_RESULT const QChar operator[](qsizetype n) const
    { return Q_ASSERT10(n >= 0), Q_ASSERT10(n < size()), at(n); }

    inline operator QString() const
    {
        return toString();
    }

private:
    static Q_DECL_CONSTEXPR inline char toHexUpper(uint value) Q_DECL_NOTHROW
    {
        return "0123456789ABCDEF"[value & 0xF];
    }
};

template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator==(QEmuStringView lhs, const Char rhs) Q_DECL_NOTHROW { return lhs == QEmuStringView(rhs,1); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator!=(QEmuStringView lhs, const Char rhs) Q_DECL_NOTHROW { return lhs != QEmuStringView(rhs,1); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator< (QEmuStringView lhs, const Char rhs) Q_DECL_NOTHROW { return lhs <  QEmuStringView(rhs,1); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator<=(QEmuStringView lhs, const Char rhs) Q_DECL_NOTHROW { return lhs <= QEmuStringView(rhs,1); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator> (QEmuStringView lhs, const Char rhs) Q_DECL_NOTHROW { return lhs >  QEmuStringView(rhs,1); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator>=(QEmuStringView lhs, const Char rhs) Q_DECL_NOTHROW { return lhs >= QEmuStringView(rhs,1); }

template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator==(const Char lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs,1) == rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator!=(const Char lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs,1) != rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator< (const Char lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs,1) <  rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator<=(const Char lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs,1) <= rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator> (const Char lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs,1) >  rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator>=(const Char lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs,1) >= rhs; }

template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator==(QEmuStringView lhs, const Char* rhs) Q_DECL_NOTHROW { return lhs == QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator!=(QEmuStringView lhs, const Char* rhs) Q_DECL_NOTHROW { return lhs != QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator< (QEmuStringView lhs, const Char* rhs) Q_DECL_NOTHROW { return lhs <  QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator<=(QEmuStringView lhs, const Char* rhs) Q_DECL_NOTHROW { return lhs <= QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator> (QEmuStringView lhs, const Char* rhs) Q_DECL_NOTHROW { return lhs >  QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator>=(QEmuStringView lhs, const Char* rhs) Q_DECL_NOTHROW { return lhs >= QEmuStringView(rhs); }

template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator==(const Char* lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) == rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator!=(const Char* lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) != rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator< (const Char* lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) <  rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator<=(const Char* lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) <= rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator> (const Char* lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) >  rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator>=(const Char* lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) >= rhs; }

template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator==(QEmuStringView lhs, const std::basic_string<Char> rhs) Q_DECL_NOTHROW { return lhs == QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator!=(QEmuStringView lhs, const std::basic_string<Char> rhs) Q_DECL_NOTHROW { return lhs != QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator< (QEmuStringView lhs, const std::basic_string<Char> rhs) Q_DECL_NOTHROW { return lhs <  QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator<=(QEmuStringView lhs, const std::basic_string<Char> rhs) Q_DECL_NOTHROW { return lhs <= QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator> (QEmuStringView lhs, const std::basic_string<Char> rhs) Q_DECL_NOTHROW { return lhs >  QEmuStringView(rhs); }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator>=(QEmuStringView lhs, const std::basic_string<Char> rhs) Q_DECL_NOTHROW { return lhs >= QEmuStringView(rhs); }

template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator==(const std::basic_string<Char> lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) == rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator!=(const std::basic_string<Char> lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) != rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator< (const std::basic_string<Char> lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) <  rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator<=(const std::basic_string<Char> lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) <= rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator> (const std::basic_string<Char> lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) >  rhs; }
template <typename Char, QEmuStringView::if_compatible_char<Char> = true>
inline bool operator>=(const std::basic_string<Char> lhs, QEmuStringView rhs) Q_DECL_NOTHROW { return QEmuStringView(lhs) >= rhs; }

#ifndef QSTRINGVIEW_H
    namespace QTest
    {

        template <> inline char *toString(const QEmuStringView &str)
        {
            return str.toPrettyUnicode();
        }

        template <typename T1, typename T2>
        inline bool qCompare(const T1 &t1, const T2 &t2, const char *actual, const char *expected,
                             const char *file, int line)
        {
            return compare_helper(t1 == t2, "Compared values are not the same",
                                  toString(t1), toString(t2), actual, expected, file, line);
        }

    }
#endif

#ifdef QSTRINGVIEW_EMULATE
// using QEmuStringView = QString;
#define QStringView QEmuStringView
#undef QStringViewLiteral
#define QStringViewLiteral(str) QStringView(QStringLiteral(str))
#define constexpr /**/
#define TESTCLASS tst_QEmuStringView
#else
#include <QStringView>
#define TESTCLASS tst_QStringView
#endif

template <typename T>
using CanConvert = std::is_convertible<T, QStringView>;


#ifdef QSTRINGVIEW_EMULATE
template <typename T>
using CanConvertParent = std::is_convertible<T, QString>;
#endif

Q_STATIC_ASSERT(!CanConvert<QLatin1String>::value);
Q_STATIC_ASSERT(!CanConvert<const char*>::value);
Q_STATIC_ASSERT(!CanConvert<QByteArray>::value);

// QStringView qchar_does_not_compile() { return QStringView(QChar('a')); }
// QStringView qlatin1string_does_not_compile() { return QStringView(QLatin1String("a")); }
// QStringView const_char_star_does_not_compile() { return QStringView("a"); }
// QStringView qbytearray_does_not_compile() { return QStringView(QByteArray("a")); }

//
// QChar
//

Q_STATIC_ASSERT(!CanConvert<QChar>::value);

Q_STATIC_ASSERT(CanConvert<QChar[123]>::value);

Q_STATIC_ASSERT(CanConvert<      QString >::value);
Q_STATIC_ASSERT(CanConvert<const QString >::value);
Q_STATIC_ASSERT(CanConvert<      QString&>::value);
Q_STATIC_ASSERT(CanConvert<const QString&>::value);

Q_STATIC_ASSERT(CanConvert<      QStringRef >::value);
Q_STATIC_ASSERT(CanConvert<const QStringRef >::value);
Q_STATIC_ASSERT(CanConvert<      QStringRef&>::value);
Q_STATIC_ASSERT(CanConvert<const QStringRef&>::value);


//
// ushort
//

Q_STATIC_ASSERT(!CanConvert<ushort>::value);

Q_STATIC_ASSERT(CanConvert<ushort[123]>::value);

Q_STATIC_ASSERT(CanConvert<      ushort*>::value);
Q_STATIC_ASSERT(CanConvert<const ushort*>::value);


//
// char16_t
//

#if defined(Q_COMPILER_UNICODE_STRINGS)

Q_STATIC_ASSERT(!CanConvert<char16_t>::value);

Q_STATIC_ASSERT(CanConvert<      char16_t*>::value);
Q_STATIC_ASSERT(CanConvert<const char16_t*>::value);

#endif

#if defined(Q_STDLIB_UNICODE_STRINGS)

Q_STATIC_ASSERT(CanConvert<      std::u16string >::value);
Q_STATIC_ASSERT(CanConvert<const std::u16string >::value);
Q_STATIC_ASSERT(CanConvert<      std::u16string&>::value);
Q_STATIC_ASSERT(CanConvert<const std::u16string&>::value);

#endif

//
// wchar_t
//

const bool CanConvertFromWCharT =
#ifdef Q_OS_WIN
        true
#else
        false
#endif
        ;

Q_STATIC_ASSERT(!CanConvert<wchar_t>::value);

#ifndef QSTRINGVIEW_EMULATE
Q_STATIC_ASSERT(CanConvert<      wchar_t*>::value == CanConvertFromWCharT);
Q_STATIC_ASSERT(CanConvert<const wchar_t*>::value == CanConvertFromWCharT);

Q_STATIC_ASSERT(CanConvert<      std::wstring >::value == CanConvertFromWCharT);
Q_STATIC_ASSERT(CanConvert<const std::wstring >::value == CanConvertFromWCharT);
Q_STATIC_ASSERT(CanConvert<      std::wstring&>::value == CanConvertFromWCharT);
Q_STATIC_ASSERT(CanConvert<const std::wstring&>::value == CanConvertFromWCharT);
#else
Q_STATIC_ASSERT(CanConvert<      wchar_t*>::value);
Q_STATIC_ASSERT(CanConvert<const wchar_t*>::value);

Q_STATIC_ASSERT(CanConvert<      std::wstring >::value);
Q_STATIC_ASSERT(CanConvert<const std::wstring >::value);
Q_STATIC_ASSERT(CanConvert<      std::wstring&>::value);
Q_STATIC_ASSERT(CanConvert<const std::wstring&>::value);
#endif


class TESTCLASS : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void constExpr() const;
    void basics() const;
    void literals() const;
    void at() const;

    void fromQString() const;
    void fromQStringRef() const;

    void fromQCharStar() const
    {
        const QChar str[] = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!', 0 };
        fromLiteral(str);
    }

    void fromUShortStar() const
    {
        const ushort str[] = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!', 0 };
        fromLiteral(str);
    }

    void fromChar16TStar() const
    {
#if defined(Q_COMPILER_UNICODE_STRINGS)
        fromLiteral(u"Hello, World!");
#else
        QSKIP("This test requires C++11 char16_t support enabled in the compiler");
#endif
    }

    void fromWCharTStar() const
    {
#ifdef Q_OS_WIN
        fromLiteral(L"Hello, World!");
#else
        QSKIP("This is a Windows-only test");
#endif
    }

    void fromQCharRange() const
    {
        const QChar str[] = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!' };
        fromRange(std::begin(str), std::end(str));
    }

    void fromUShortRange() const
    {
        const ushort str[] = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!' };
        fromRange(std::begin(str), std::end(str));
    }

    void fromChar16TRange() const
    {
#if defined(Q_COMPILER_UNICODE_STRINGS)
        const char16_t str[] = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!' };
        fromRange(std::begin(str), std::end(str));
#else
        QSKIP("This test requires C++11 char16_t support enabled in the compiler");
#endif
    }

    void fromWCharTRange() const
    {
#ifdef Q_OS_WIN
        const wchar_t str[] = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!' };
        fromRange(std::begin(str), std::end(str));
#else
        QSKIP("This is a Windows-only test");
#endif
    }

    // std::basic_string
    void fromStdStringWCharT() const
    {
#ifdef Q_OS_WIN
        fromStdString<wchar_t>();
#else
        QSKIP("This is a Windows-only test");
#endif
    }
    void fromStdStringChar16T() const
    {
#ifdef Q_STDLIB_UNICODE_STRINGS
        fromStdString<char16_t>();
#else
        QSKIP("This test requires C++11 char16_t support enabled in compiler & stdlib");
#endif
    }

private:
    template <typename String>
    void conversion_tests(String arg) const;
    template <typename Char>
    void fromLiteral(const Char *arg) const;
    template <typename Char>
    void fromRange(const Char *first, const Char *last) const;
    template <typename Char, typename Container>
    void fromContainer() const;
    template <typename Char>
    void fromStdString() const { fromContainer<Char, std::basic_string<Char> >(); }
};

#ifdef QSTRINGVIEW_EMULATE
#undef Q_STATIC_ASSERT
#define Q_STATIC_ASSERT Q_ASSERT
#endif

void TESTCLASS::constExpr() const
{
    // compile-time checks
#ifdef Q_COMPILER_CONSTEXPR
    {
        constexpr QStringView sv;
        Q_STATIC_ASSERT(sv.size() == 0);
        Q_STATIC_ASSERT(sv.isNull());
        Q_STATIC_ASSERT(sv.empty());
        Q_STATIC_ASSERT(sv.isEmpty());
        Q_STATIC_ASSERT(sv.utf16() == nullptr);

        constexpr QStringView sv2(sv.utf16(), sv.utf16() + sv.size());
        Q_STATIC_ASSERT(sv2.isNull());
        Q_STATIC_ASSERT(sv2.empty());
    }
    {
        constexpr QStringView sv = QStringViewLiteral("");
        Q_STATIC_ASSERT(sv.size() == 0);
        Q_STATIC_ASSERT(!sv.isNull());
        Q_STATIC_ASSERT(sv.empty());
        Q_STATIC_ASSERT(sv.isEmpty());
        Q_STATIC_ASSERT(sv.utf16() != nullptr);

        constexpr QStringView sv2(sv.utf16(), sv.utf16() + sv.size());
        Q_STATIC_ASSERT(!sv2.isNull());
        Q_STATIC_ASSERT(sv2.empty());
    }
    {
        constexpr QStringView sv = QStringViewLiteral("Hello");
        Q_STATIC_ASSERT(sv.size() == 5);
        Q_STATIC_ASSERT(!sv.empty());
        Q_STATIC_ASSERT(!sv.isEmpty());
        Q_STATIC_ASSERT(!sv.isNull());
        Q_STATIC_ASSERT(*sv.utf16() == 'H');
        Q_STATIC_ASSERT(sv[0]      == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv.at(0)   == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv.front() == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv.first() == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv[4]      == QLatin1Char('o'));
        Q_STATIC_ASSERT(sv.at(4)   == QLatin1Char('o'));
        Q_STATIC_ASSERT(sv.back()  == QLatin1Char('o'));
        Q_STATIC_ASSERT(sv.last()  == QLatin1Char('o'));

        constexpr QStringView sv2(sv.utf16(), sv.utf16() + sv.size());
        Q_STATIC_ASSERT(!sv2.isNull());
        Q_STATIC_ASSERT(!sv2.empty());
        Q_STATIC_ASSERT(sv2.size() == 5);
    }
#if !defined(Q_OS_WIN) || defined(Q_COMPILER_UNICODE_STRINGS)
    {
        Q_STATIC_ASSERT(QStringView(u"Hello").size() == 5);
        constexpr QStringView sv = u"Hello";
        Q_STATIC_ASSERT(sv.size() == 5);
        Q_STATIC_ASSERT(!sv.empty());
        Q_STATIC_ASSERT(!sv.isEmpty());
        Q_STATIC_ASSERT(!sv.isNull());
        Q_STATIC_ASSERT(*sv.utf16() == 'H');
        Q_STATIC_ASSERT(sv[0]      == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv.at(0)   == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv.front() == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv.first() == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv[4]      == QLatin1Char('o'));
        Q_STATIC_ASSERT(sv.at(4)   == QLatin1Char('o'));
        Q_STATIC_ASSERT(sv.back()  == QLatin1Char('o'));
        Q_STATIC_ASSERT(sv.last()  == QLatin1Char('o'));

        constexpr QStringView sv2(sv.utf16(), sv.utf16() + sv.size());
        Q_STATIC_ASSERT(!sv2.isNull());
        Q_STATIC_ASSERT(!sv2.empty());
        Q_STATIC_ASSERT(sv2.size() == 5);

        constexpr char16_t *null = nullptr;
        constexpr QStringView sv3(null);
        Q_STATIC_ASSERT(sv3.isNull());
        Q_STATIC_ASSERT(sv3.isEmpty());
        Q_STATIC_ASSERT(sv3.size() == 0);
    }
#else // storage_type is wchar_t
    {
        Q_STATIC_ASSERT(QStringView(L"Hello").size() == 5);
        constexpr QStringView sv = L"Hello";
        Q_STATIC_ASSERT(sv.size() == 5);
        Q_STATIC_ASSERT(!sv.empty());
        Q_STATIC_ASSERT(!sv.isEmpty());
        Q_STATIC_ASSERT(!sv.isNull());
        Q_STATIC_ASSERT(*sv.utf16() == 'H');
        Q_STATIC_ASSERT(sv[0]      == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv.at(0)   == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv.front() == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv.first() == QLatin1Char('H'));
        Q_STATIC_ASSERT(sv[4]      == QLatin1Char('o'));
        Q_STATIC_ASSERT(sv.at(4)   == QLatin1Char('o'));
        Q_STATIC_ASSERT(sv.back()  == QLatin1Char('o'));
        Q_STATIC_ASSERT(sv.last()  == QLatin1Char('o'));

        constexpr QStringView sv2(sv.utf16(), sv.utf16() + sv.size());
        Q_STATIC_ASSERT(!sv2.isNull());
        Q_STATIC_ASSERT(!sv2.empty());
        Q_STATIC_ASSERT(sv2.size() == 5);

        constexpr wchar_t *null = nullptr;
        constexpr QStringView sv3(null);
        Q_STATIC_ASSERT(sv3.isNull());
        Q_STATIC_ASSERT(sv3.isEmpty());
        Q_STATIC_ASSERT(sv3.size() == 0);
    }
#endif
#endif
}

void TESTCLASS::basics() const
{
    QStringView sv1;

    // a default-constructed QStringView is null:
    QVERIFY(sv1.isNull());
    // which implies it's empty();
    QVERIFY(sv1.isEmpty());

    QStringView sv2;

    QVERIFY(sv2 == sv1);
    QVERIFY(!(sv2 != sv1));
}

void TESTCLASS::literals() const
{
#if !defined(Q_OS_WIN) || defined(Q_COMPILER_UNICODE_STRINGS)
    const char16_t hello[] = u"Hello";
    const char16_t longhello[] =
            u"Hello World. This is a much longer message, to exercise qustrlen.";
    const char16_t withnull[] = u"a\0zzz";
#else // storage_type is wchar_t
    const wchar_t hello[] = L"Hello";
    const wchar_t longhello[] =
            L"Hello World. This is a much longer message, to exercise qustrlen.";
    const wchar_t withnull[] = L"a\0zzz";
#endif
    Q_STATIC_ASSERT(sizeof(longhello) >= 16);

    QCOMPARE(QStringView(hello).size(), 5);
    QCOMPARE(QStringView(hello + 0).size(), 5); // forces decay to pointer
    QStringView sv = hello;
    qWarning() << "sv=" << sv.toString();
    QCOMPARE(sv.size(), 5);
    QVERIFY(!sv.empty());
    QVERIFY(!sv.isEmpty());
    QVERIFY(!sv.isNull());
    QCOMPARE(*sv.utf16(), 'H');
    QCOMPARE(sv[0],      QLatin1Char('H'));
    QCOMPARE(sv.at(0),   QLatin1Char('H'));
    QCOMPARE(sv.front(), QLatin1Char('H'));
    QCOMPARE(sv.first(), QLatin1Char('H'));
    QCOMPARE(sv[4],      QLatin1Char('o'));
    QCOMPARE(sv.at(4),   QLatin1Char('o'));
    QCOMPARE(sv.back(),  QLatin1Char('o'));
    QCOMPARE(sv.last(),  QLatin1Char('o'));

    QStringView sv2(sv.utf16(), sv.utf16() + sv.size());
    qWarning() << "sv2=" << sv2;
    QVERIFY(!sv2.isNull());
    QVERIFY(!sv2.empty());
    QCOMPARE(sv2.size(), 5);

    QStringView sv3(longhello);
    qWarning() << "sv3=" << sv3.toString();
    QCOMPARE(size_t(sv3.size()), sizeof(longhello)/sizeof(longhello[0]) - 1);
    QCOMPARE(sv3.last(), QLatin1Char('.'));
    sv3 = longhello;
    QCOMPARE(size_t(sv3.size()), sizeof(longhello)/sizeof(longhello[0]) - 1);

    for (int i = 0; i < sv3.size(); ++i) {
        QStringView sv4(longhello + i);
        QCOMPARE(size_t(sv4.size()), sizeof(longhello)/sizeof(longhello[0]) - 1 - i);
        QCOMPARE(sv4.last(), QLatin1Char('.'));
        sv4 = longhello + i;
        QCOMPARE(size_t(sv4.size()), sizeof(longhello)/sizeof(longhello[0]) - 1 - i);
    }

    // these are different results
    qWarning() << "QStringView(withnull)=" << QStringView(withnull);
    QCOMPARE(size_t(QStringView(withnull).size()), sizeof(withnull)/sizeof(withnull[0]) - 1);
    QCOMPARE(QStringView(withnull + 0).size(), 1);
}

void TESTCLASS::at() const
{
    QString hello("Hello");
    QStringView sv(hello);
    QCOMPARE(sv.at(0), QChar('H')); QCOMPARE(sv[0], QChar('H'));
    QCOMPARE(sv.at(1), QChar('e')); QCOMPARE(sv[1], QChar('e'));
    QCOMPARE(sv.at(2), QChar('l')); QCOMPARE(sv[2], QChar('l'));
    QCOMPARE(sv.at(3), QChar('l')); QCOMPARE(sv[3], QChar('l'));
    QCOMPARE(sv.at(4), QChar('o')); QCOMPARE(sv[4], QChar('o'));
}

void TESTCLASS::fromQString() const
{
    QString null;
    QString empty = "";

    QVERIFY( QStringView(null).isNull());
    QVERIFY( QStringView(null).isEmpty());
    QVERIFY( QStringView(empty).isEmpty());
    QVERIFY(!QStringView(empty).isNull());

    conversion_tests(QString("Hello World!"));
}

void TESTCLASS::fromQStringRef() const
{
    QStringRef null;
    QString emptyS = "";
    QStringRef empty(&emptyS);

    QVERIFY( QStringView(null).isNull());
    QVERIFY( QStringView(null).isEmpty());
    QVERIFY( QStringView(empty).isEmpty());
    QVERIFY(!QStringView(empty).isNull());

    conversion_tests(QString("Hello World!").midRef(6));
}

template <typename Char>
void TESTCLASS::fromLiteral(const Char *arg) const
{
    const Char *null = nullptr;
    const Char empty[] = { 0 };

    QCOMPARE(QStringView(null).size(), qsizetype(0));
    QCOMPARE(QStringView(null).data(), nullptr);
    QCOMPARE(QStringView(empty).size(), qsizetype(0));
    QCOMPARE(static_cast<const void*>(QStringView(empty).data()),
             static_cast<const void*>(empty));

    QVERIFY( QStringView(null).isNull());
    QVERIFY( QStringView(null).isEmpty());
    QVERIFY( QStringView(empty).isEmpty());
    QVERIFY(!QStringView(empty).isNull());

    conversion_tests(arg);
}

template <typename Char>
void TESTCLASS::fromRange(const Char *first, const Char *last) const
{
    const Char *null = nullptr;
    QCOMPARE(QStringView(null, null).size(), 0);
    QCOMPARE(QStringView(null, null).data(), nullptr);
    QCOMPARE(QStringView(first, first).size(), 0);
    QCOMPARE(static_cast<const void*>(QStringView(first, first).data()),
             static_cast<const void*>(first));

    const auto sv = QStringView(first, last);
    QCOMPARE(sv.size(), last - first);
    QCOMPARE(static_cast<const void*>(sv.data()),
             static_cast<const void*>(first));

    // can't call conversion_tests() here, as it requires a single object
}

template <typename Char, typename Container>
void TESTCLASS::fromContainer() const
{
    const QString s = "Hello World!";

    Container c;
    // unspecified whether empty containers make null QStringViews
    QVERIFY(QStringView(c).isEmpty());

    QCOMPARE(sizeof(Char), sizeof(QChar));

    const auto *data = reinterpret_cast<const Char *>(s.utf16());
    std::copy(data, data + s.size(), std::back_inserter(c));
    conversion_tests(std::move(c));
}

namespace help {
template <typename T>
size_t size(const T &t) { return size_t(t.size()); }
template <typename T>
size_t size(const T *t)
{
    size_t result = 0;
    if (t) {
        while (*t++)
            ++result;
    }
    return result;
}
size_t size(const QChar *t)
{
    size_t result = 0;
    if (t) {
        while (!t++->isNull())
            ++result;
    }
    return result;
}

template <typename T>
typename T::const_iterator cbegin(const T &t) { return t.cbegin(); }
template <typename T>
const T *                  cbegin(const T *t) { return t; }

template <typename T>
typename T::const_iterator cend(const T &t) { return t.cend(); }
template <typename T>
const T *                  cend(const T *t) { return t + size(t); }

template <typename T>
typename T::const_reverse_iterator crbegin(const T &t) { return t.crbegin(); }
template <typename T>
std::reverse_iterator<const T*>    crbegin(const T *t) { return std::reverse_iterator<const T*>(cend(t)); }

template <typename T>
typename T::const_reverse_iterator crend(const T &t) { return t.crend(); }
template <typename T>
std::reverse_iterator<const T*>    crend(const T *t) { return std::reverse_iterator<const T*>(cbegin(t)); }

} // namespace help

template <typename String>
void TESTCLASS::conversion_tests(String string) const
{
    // copy-construct:
    {
        QStringView sv = string;

        QCOMPARE(help::size(sv), help::size(string));

        // check iterators:

        QVERIFY(std::equal(help::cbegin(string), help::cend(string),
                           QT_MAKE_CHECKED_ARRAY_ITERATOR(sv.cbegin(), sv.size())));
        QVERIFY(std::equal(help::cbegin(string), help::cend(string),
                           QT_MAKE_CHECKED_ARRAY_ITERATOR(sv.begin(), sv.size())));
        QVERIFY(std::equal(help::crbegin(string), help::crend(string),
                           sv.crbegin()));
        QVERIFY(std::equal(help::crbegin(string), help::crend(string),
                           sv.rbegin()));

        QCOMPARE(sv, string);
    }

    QStringView sv;

    // copy-assign:
    {
        sv = string;

        QCOMPARE(help::size(sv), help::size(string));

        // check relational operators:

        QCOMPARE(sv, string);
        QCOMPARE(string, sv);

        QVERIFY(!(sv != string));
        QVERIFY(!(string != sv));

        QVERIFY(!(sv < string));
        QVERIFY(sv <= string);
        QVERIFY(!(sv > string));
        QVERIFY(sv >= string);

        QVERIFY(!(string < sv));
        QVERIFY(string <= sv);
        QVERIFY(!(string > sv));
        QVERIFY(string >= sv);
    }

    // copy-construct from rvalue (QStringView never assumes ownership):
    {
        QStringView sv2 = std::move(string);
        QCOMPARE(sv2, sv);
        QCOMPARE(sv2, string);
    }

    // copy-assign from rvalue (QStringView never assumes ownership):
    {
        QStringView sv2;
        sv2 = std::move(string);
        QCOMPARE(sv2, sv);
        QCOMPARE(sv2, string);
    }
}

QTEST_APPLESS_MAIN(TESTCLASS)
#include "tst_qstringview.moc"
