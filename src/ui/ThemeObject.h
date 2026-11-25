#pragma once

#include "core/ThemeData.h"

#include <QColor>
#include <QObject>
#include <QString>

namespace nrs {

class ThemeObject : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(bool rounded READ rounded NOTIFY changed)
    Q_PROPERTY(bool rememberWindowSize READ rememberWindowSize NOTIFY changed)
    Q_PROPERTY(QString locale READ locale NOTIFY changed)
    Q_PROPERTY(double cardRadius READ cardRadius NOTIFY changed)
    Q_PROPERTY(double controlRadius READ controlRadius NOTIFY changed)
    Q_PROPERTY(int fontSizeTitle READ fontSizeTitle NOTIFY changed)
    Q_PROPERTY(int fontSizeSection READ fontSizeSection NOTIFY changed)
    Q_PROPERTY(int fontSizeBody READ fontSizeBody NOTIFY changed)
    Q_PROPERTY(int fontSizeSmall READ fontSizeSmall NOTIFY changed)
    Q_PROPERTY(int fontSizeCaption READ fontSizeCaption NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor layer READ layer NOTIFY changed)
    Q_PROPERTY(QColor page READ page NOTIFY changed)
    Q_PROPERTY(QColor windowEdge READ windowEdge NOTIFY changed)
    Q_PROPERTY(QColor divider READ divider NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor subtext READ subtext NOTIFY changed)
    Q_PROPERTY(QColor faint READ faint NOTIFY changed)
    Q_PROPERTY(QColor hover READ hover NOTIFY changed)
    Q_PROPERTY(QColor pressed READ pressed NOTIFY changed)
    Q_PROPERTY(QColor control READ control NOTIFY changed)
    Q_PROPERTY(QColor controlHover READ controlHover NOTIFY changed)
    Q_PROPERTY(QColor controlBorder READ controlBorder NOTIFY changed)
    Q_PROPERTY(QColor inputFocused READ inputFocused NOTIFY changed)
    Q_PROPERTY(QColor menuBackground READ menuBackground NOTIFY changed)
    Q_PROPERTY(QColor menuHover READ menuHover NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentFill READ accentFill NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor sidebarBackground READ sidebarBackground NOTIFY changed)
    Q_PROPERTY(QColor cardSurface READ cardSurface NOTIFY changed)
    Q_PROPERTY(QColor softBorder READ softBorder NOTIFY changed)
    Q_PROPERTY(QColor successBg READ successBg NOTIFY changed)
    Q_PROPERTY(QColor successBorder READ successBorder NOTIFY changed)
    Q_PROPERTY(QColor successText READ successText NOTIFY changed)
    Q_PROPERTY(QColor warningBg READ warningBg NOTIFY changed)
    Q_PROPERTY(QColor warningBorder READ warningBorder NOTIFY changed)
    Q_PROPERTY(QColor warningText READ warningText NOTIFY changed)
    Q_PROPERTY(QColor infoBg READ infoBg NOTIFY changed)
    Q_PROPERTY(QColor infoBorder READ infoBorder NOTIFY changed)
    Q_PROPERTY(QColor infoText READ infoText NOTIFY changed)
    Q_PROPERTY(QColor activeCardBg READ activeCardBg NOTIFY changed)
    Q_PROPERTY(QColor activeCardBorder READ activeCardBorder NOTIFY changed)
    Q_PROPERTY(QColor badgeNeutralBg READ badgeNeutralBg NOTIFY changed)
    Q_PROPERTY(QColor badgeNeutralText READ badgeNeutralText NOTIFY changed)

public:
    explicit ThemeObject(QObject* parent = nullptr);

    void update(const ThemeData& data);
    const ThemeData& data() const { return m_data; }

    bool dark() const { return m_data.dark; }
    bool rounded() const { return m_data.rounded; }
    bool rememberWindowSize() const { return m_data.rememberWindowSize; }
    QString locale() const { return m_data.locale; }
    double cardRadius() const { return m_data.cardRadius; }
    double controlRadius() const { return m_data.controlRadius; }
    int fontSizeTitle() const { return 16; }
    int fontSizeSection() const { return 14; }
    int fontSizeBody() const { return 13; }
    int fontSizeSmall() const { return 12; }
    int fontSizeCaption() const { return 11; }
    QColor background() const { return QColor(m_data.background); }
    QColor layer() const { return QColor(m_data.layer); }
    QColor page() const { return QColor(m_data.page); }
    QColor windowEdge() const { return QColor(m_data.windowEdge); }
    QColor divider() const { return QColor(m_data.divider); }
    QColor text() const { return QColor(m_data.text); }
    QColor subtext() const { return QColor(m_data.subtext); }
    QColor faint() const { return QColor(m_data.faint); }
    QColor hover() const { return QColor(m_data.hover); }
    QColor pressed() const { return QColor(m_data.pressed); }
    QColor control() const { return QColor(m_data.control); }
    QColor controlHover() const { return QColor(m_data.controlHover); }
    QColor controlBorder() const { return QColor(m_data.controlBorder); }
    QColor inputFocused() const { return QColor(m_data.inputFocused); }
    QColor menuBackground() const { return QColor(m_data.menuBackground); }
    QColor menuHover() const { return QColor(m_data.menuHover); }
    QColor accent() const { return QColor(m_data.accent); }
    QColor accentFill() const { return QColor(m_data.accentFill); }
    QColor accentText() const { return QColor(m_data.accentText); }
    QColor danger() const { return QColor(m_data.danger); }
    QColor surface() const;
    QColor sidebarBackground() const;
    QColor cardSurface() const;
    QColor softBorder() const;
    QColor successBg() const;
    QColor successBorder() const;
    QColor successText() const;
    QColor warningBg() const;
    QColor warningBorder() const;
    QColor warningText() const;
    QColor infoBg() const;
    QColor infoBorder() const;
    QColor infoText() const;
    QColor activeCardBg() const;
    QColor activeCardBorder() const;
    QColor badgeNeutralBg() const;
    QColor badgeNeutralText() const;

    Q_INVOKABLE QColor withAlpha(const QColor& color, qreal alpha) const;

signals:
    void changed();

private:
    ThemeData m_data;
};

} // namespace nrs
