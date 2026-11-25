#include "ui/ThemeObject.h"

#include <algorithm>
#include <tuple>

namespace nrs {

namespace {

auto themeTuple(const ThemeData& d)
{
    return std::tie(
        d.dark, d.rounded, d.rememberWindowSize, d.locale,
        d.cardRadius, d.controlRadius,
        d.background, d.layer, d.page, d.windowEdge, d.divider,
        d.text, d.subtext, d.faint, d.hover, d.pressed,
        d.control, d.controlHover, d.controlBorder, d.inputFocused,
        d.menuBackground, d.menuHover,
        d.accent, d.accentFill, d.accentText, d.danger);
}

QColor opaquePageColor(const ThemeData& d)
{
    return QColor(blendOverOpaque(d.page, defaultPageBaseHex(d.dark)));
}

QColor opaqueLayerColor(const ThemeData& d)
{
    return QColor(blendOverOpaque(d.layer, opaquePageColor(d).name(QColor::HexArgb)));
}

} // namespace

ThemeObject::ThemeObject(QObject* parent)
    : QObject(parent)
{
}

void ThemeObject::update(const ThemeData& data)
{
    if (themeTuple(m_data) == themeTuple(data)) {
        return;
    }
    m_data = data;
    emit changed();
}

QColor ThemeObject::withAlpha(const QColor& color, qreal alpha) const
{
    QColor copy = color;
    const double factor = std::clamp(static_cast<double>(alpha), 0.0, 1.0);
    copy.setAlphaF(std::clamp(copy.alphaF() * factor, 0.0, 1.0));
    return copy;
}

QColor ThemeObject::surface() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFFFFF"));
    }
    return blendColors(QColor(QStringLiteral("#FFFFFFFF")), opaqueLayerColor(m_data), 0.06);
}

QColor ThemeObject::sidebarBackground() const
{
    const QColor pageColor = opaquePageColor(m_data);
    if (!m_data.dark) {
        return blendColors(QColor(QStringLiteral("#FFEEF1F6")), pageColor, 0.80);
    }
    return blendColors(QColor(QStringLiteral("#FF181A1F")), pageColor, 0.75);
}

QColor ThemeObject::cardSurface() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFF8FAFC"));
    }
    return blendColors(QColor(QStringLiteral("#FF2C2F37")), opaqueLayerColor(m_data), 0.70);
}

QColor ThemeObject::softBorder() const
{
    const QColor pageColor = opaquePageColor(m_data);
    if (!m_data.dark) {
        return blendColors(QColor(QStringLiteral("#FF0F172A")), pageColor, 0.09);
    }
    return blendColors(QColor(QStringLiteral("#FFFFFFFF")), pageColor, 0.10);
}

QColor ThemeObject::successBg() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFECFDF5"));
    }
    return blendColors(QColor(QStringLiteral("#FF10B981")), opaqueLayerColor(m_data), 0.16);
}

QColor ThemeObject::successBorder() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFA7F3D0"));
    }
    return blendColors(QColor(QStringLiteral("#FF34D399")), opaqueLayerColor(m_data), 0.42);
}

QColor ThemeObject::successText() const
{
    return QColor(m_data.dark ? QStringLiteral("#FF6EE7B7") : QStringLiteral("#FF047857"));
}

QColor ThemeObject::warningBg() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFFEF3C7"));
    }
    return blendColors(QColor(QStringLiteral("#FFF59E0B")), opaqueLayerColor(m_data), 0.18);
}

QColor ThemeObject::warningBorder() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFF59E0B"));
    }
    return blendColors(QColor(QStringLiteral("#FFFBBF24")), opaqueLayerColor(m_data), 0.48);
}

QColor ThemeObject::warningText() const
{
    return QColor(m_data.dark ? QStringLiteral("#FFFCD34D") : QStringLiteral("#FF92400E"));
}

QColor ThemeObject::infoBg() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFDBEAFE"));
    }
    return blendColors(accent(), opaqueLayerColor(m_data), 0.18);
}

QColor ThemeObject::infoBorder() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FF93C5FD"));
    }
    return blendColors(accent(), opaqueLayerColor(m_data), 0.45);
}

QColor ThemeObject::infoText() const
{
    return QColor(m_data.dark ? QStringLiteral("#FF93C5FD") : QStringLiteral("#FF1E40AF"));
}

QColor ThemeObject::activeCardBg() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFFFFFFF"));
    }
    return blendColors(accent(), surface(), 0.08);
}

QColor ThemeObject::activeCardBorder() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FF3B82F6"));
    }
    return blendColors(accent(), surface(), 0.78);
}

QColor ThemeObject::badgeNeutralBg() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFF1F5F9"));
    }
    return blendColors(QColor(QStringLiteral("#FFFFFFFF")), surface(), 0.08);
}

QColor ThemeObject::badgeNeutralText() const
{
    return QColor(m_data.dark ? QStringLiteral("#FFCBD5E1") : QStringLiteral("#FF475569"));
}

} // namespace nrs
