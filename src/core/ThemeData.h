#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QtMath>

#include <algorithm>

namespace nrs {

inline QString defaultPageBaseHex(bool dark)
{
    return dark ? QStringLiteral("#FF1C1E22") : QStringLiteral("#FFF8F9FB");
}

inline QString formatOpaqueHex(const QColor& color)
{
    QColor c = color.isValid() ? color : QColor(Qt::black);
    c.setAlpha(255);
    return c.name(QColor::HexArgb).toUpper();
}

inline QColor blendColors(const QColor& fg, const QColor& bg, double fgWeight)
{
    const QColor safeBg = bg.isValid() ? bg : QColor(Qt::black);
    if (!fg.isValid()) {
        QColor out = safeBg;
        out.setAlpha(255);
        return out;
    }
    const double w = std::clamp(fgWeight, 0.0, 1.0);
    const int r = std::clamp(qRound(fg.red() * w + safeBg.red() * (1.0 - w)), 0, 255);
    const int g = std::clamp(qRound(fg.green() * w + safeBg.green() * (1.0 - w)), 0, 255);
    const int b = std::clamp(qRound(fg.blue() * w + safeBg.blue() * (1.0 - w)), 0, 255);
    return QColor(r, g, b, 255);
}

inline QString blendOverOpaque(const QString& fgHex, const QString& bgHex)
{
    const QColor fg(fgHex);
    const QColor bg(bgHex);
    if (!fg.isValid()) {
        return formatOpaqueHex(bg);
    }
    if (fg.alpha() >= 255) {
        return formatOpaqueHex(fg);
    }
    return formatOpaqueHex(blendColors(fg, bg, static_cast<double>(fg.alpha()) / 255.0));
}

struct ThemeData {
    bool dark = true;
    bool rounded = true;
    bool rememberWindowSize = true;
    QString locale = QStringLiteral("zh");
    double cardRadius = 10.0;
    double controlRadius = 6.0;
    QString background = QStringLiteral("#FF1C1D20");
    QString layer = QStringLiteral("#FF25262B");
    QString page = QStringLiteral("#FF202125");
    QString windowEdge = QStringLiteral("#FF3A3C42");
    QString divider = QStringLiteral("#FF32343A");
    QString text = QStringLiteral("#FFF2F2F5");
    QString subtext = QStringLiteral("#FFA8ABB3");
    QString faint = QStringLiteral("#FF787B84");
    QString hover = QStringLiteral("#FF2F3138");
    QString pressed = QStringLiteral("#FF393B43");
    QString control = QStringLiteral("#FF2C2E34");
    QString controlHover = QStringLiteral("#FF363840");
    QString controlBorder = QStringLiteral("#FF454852");
    QString inputFocused = QStringLiteral("#FF2AA8FF");
    QString menuBackground = QStringLiteral("#FF26272C");
    QString menuHover = QStringLiteral("#FF35373F");
    QString accent = QStringLiteral("#FF38B2FF");
    QString accentFill = QStringLiteral("#FF1F78D1");
    QString accentText = QStringLiteral("#FFFFFFFF");
    QString danger = QStringLiteral("#FFE5534B");

    static ThemeData lightDefaults()
    {
        ThemeData t;
        t.dark = false;
        t.background = QStringLiteral("#FFF5F6F8");
        t.layer = QStringLiteral("#FFFFFFFF");
        t.page = QStringLiteral("#FFF8F9FB");
        t.windowEdge = QStringLiteral("#FFD5D9E2");
        t.divider = QStringLiteral("#FFE6E9EF");
        t.text = QStringLiteral("#FF1E2024");
        t.subtext = QStringLiteral("#FF5E636E");
        t.faint = QStringLiteral("#FF8C919C");
        t.hover = QStringLiteral("#FFEDEFF4");
        t.pressed = QStringLiteral("#FFE2E6EE");
        t.control = QStringLiteral("#FFFFFFFF");
        t.controlHover = QStringLiteral("#FFF1F3F6");
        t.controlBorder = QStringLiteral("#FFD5D9E2");
        t.inputFocused = QStringLiteral("#FF0078D4");
        t.menuBackground = QStringLiteral("#FFFFFFFF");
        t.menuHover = QStringLiteral("#FFEFF2F6");
        t.accent = QStringLiteral("#FF0078D4");
        t.accentFill = QStringLiteral("#FF006CBE");
        t.accentText = QStringLiteral("#FFFFFFFF");
        t.danger = QStringLiteral("#FFC42B1C");
        return t;
    }

    static ThemeData fromAppearance(const QString& localeStr, const QString& themeStr, const QJsonObject& app)
    {
        const QJsonObject look = app.value(QStringLiteral("nativeLook")).toObject();
        const bool isDark = look.contains(QStringLiteral("dark"))
            ? look.value(QStringLiteral("dark")).toBool(true)
            : (themeStr != QLatin1String("light"));
        ThemeData t = isDark ? ThemeData{} : lightDefaults();
        t.dark = isDark;
        if (!localeStr.isEmpty()) {
            t.locale = localeStr;
        }
        if (app.contains(QStringLiteral("locale"))) {
            t.locale = app.value(QStringLiteral("locale")).toString(t.locale);
        }
        t.rememberWindowSize = app.value(QStringLiteral("rememberWindowSize")).toBool(true);
        if (look.isEmpty()) {
            const QString corners = app.value(QStringLiteral("corners")).toString();
            if (corners == QLatin1String("square")) {
                t.rounded = false;
                t.cardRadius = 0.0;
                t.controlRadius = 0.0;
            }
        } else {
            t.rounded = look.value(QStringLiteral("rounded")).toBool(true);
            t.cardRadius = t.rounded ? look.value(QStringLiteral("cardRadius")).toDouble(10.0) : 0.0;
            t.controlRadius = t.rounded ? look.value(QStringLiteral("controlRadius")).toDouble(6.0) : 0.0;
            auto pick = [&](const char* key, QString& dst) {
                const QString v = look.value(QLatin1String(key)).toString();
                if (v.startsWith(u'#') && (v.size() == 7 || v.size() == 9) && QColor(v).isValid()) {
                    dst = v;
                }
            };
            pick("background", t.background);
            pick("layer", t.layer);
            pick("page", t.page);
            pick("windowEdge", t.windowEdge);
            pick("divider", t.divider);
            pick("text", t.text);
            pick("subtext", t.subtext);
            pick("faint", t.faint);
            pick("hover", t.hover);
            pick("pressed", t.pressed);
            pick("control", t.control);
            pick("controlHover", t.controlHover);
            pick("controlBorder", t.controlBorder);
            pick("inputFocused", t.inputFocused);
            pick("menuBackground", t.menuBackground);
            pick("menuHover", t.menuHover);
            pick("accent", t.accent);
            pick("accentFill", t.accentFill);
            pick("accentText", t.accentText);
            pick("danger", t.danger);
        }

        t.page = blendOverOpaque(t.page, defaultPageBaseHex(t.dark));
        t.background = blendOverOpaque(t.background, t.page);
        t.layer = blendOverOpaque(t.layer, t.page);
        t.menuBackground = blendOverOpaque(t.menuBackground, t.layer);
        t.control = blendOverOpaque(t.control, t.layer);
        t.windowEdge = blendOverOpaque(t.windowEdge, t.page);
        t.divider = blendOverOpaque(t.divider, t.page);
        t.controlBorder = blendOverOpaque(t.controlBorder, t.page);
        t.hover = blendOverOpaque(t.hover, t.page);
        t.pressed = blendOverOpaque(t.pressed, t.page);
        t.controlHover = blendOverOpaque(t.controlHover, t.layer);
        t.menuHover = blendOverOpaque(t.menuHover, t.menuBackground);
        if (!t.dark) {
            const QColor pageColor(t.page);
            auto softenIntoPage = [&](QString& hex, int minLightness, double weight) {
                const QColor c(hex);
                if (c.isValid() && c.lightness() < minLightness) {
                    hex = formatOpaqueHex(blendColors(c, pageColor, weight));
                }
            };
            softenIntoPage(t.windowEdge, 150, 0.22);
            softenIntoPage(t.divider, 190, 0.10);
            softenIntoPage(t.controlBorder, 150, 0.22);
            softenIntoPage(t.hover, 210, 0.08);
            softenIntoPage(t.pressed, 195, 0.14);
            softenIntoPage(t.controlHover, 215, 0.07);
            softenIntoPage(t.menuHover, 210, 0.08);
        }
        return t;
    }
};

} // namespace nrs
