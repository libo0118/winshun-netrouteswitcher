import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: false
    width: 840
    height: 680
    minimumWidth: 720
    minimumHeight: 540
    color: theme.page
    title: "多网卡出口切换器 (NetRouteSwitcher)"
    palette.window: theme.surface
    palette.windowText: theme.text
    palette.base: theme.control
    palette.text: theme.text
    palette.button: theme.control
    palette.buttonText: theme.text
    palette.highlight: theme.accentFill
    palette.highlightedText: theme.accentText

    property bool disconnectedExpanded: true

    onClosing: (close) => {
        if (!controller.standaloneMode && !controller.shuttingDown) {
            close.accepted = false
            controller.hideWindow()
        }
    }

    Shortcut {
        sequence: "F5"
        enabled: root.visible && !controller.busy && !controller.dialogOpen
        onActivated: controller.refresh()
    }

    component ActionBtn: Button {
        id: abtn
        property bool primaryStyle: false
        property bool activeSuccessStyle: false
        implicitHeight: 30
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        opacity: enabled ? 1.0 : 0.50

        contentItem: Text {
            text: abtn.text
            color: abtn.activeSuccessStyle
                ? theme.successText
                : (abtn.primaryStyle ? theme.accentText : theme.text)
            font.pixelSize: theme.fontSizeSmall
            font.weight: (abtn.primaryStyle || abtn.activeSuccessStyle) ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: theme.controlRadius
            color: abtn.activeSuccessStyle
                ? theme.successBg
                : (abtn.primaryStyle
                    ? (abtn.down ? theme.pressed : (abtn.hovered ? theme.accent : theme.accentFill))
                    : (abtn.down ? theme.pressed : (abtn.hovered ? theme.controlHover : theme.control)))
            border.width: abtn.primaryStyle ? 0 : 1
            border.color: abtn.activeSuccessStyle ? theme.successBorder : theme.controlBorder
            Behavior on color { ColorAnimation { duration: 90 } }
        }
    }

    component BadgePill: Rectangle {
        id: pill
        property string labelText: ""
        property color bgColor: theme.badgeNeutralBg
        property color borderColor: theme.softBorder
        property color textColor: theme.badgeNeutralText
        property bool hasBorder: false
        property bool boldText: false
        property real pillRadius: theme.rounded ? 10 : 0
        property int horizontalPad: 8

        implicitWidth: pillLbl.implicitWidth + horizontalPad * 2
        implicitHeight: 22
        radius: pillRadius
        color: bgColor
        border.width: hasBorder ? 1 : 0
        border.color: borderColor

        Text {
            id: pillLbl
            anchors.centerIn: parent
            text: pill.labelText
            color: pill.textColor
            font.pixelSize: theme.fontSizeCaption
            font.weight: pill.boldText ? Font.DemiBold : Font.Normal
        }
    }

    component StyledCheckBox: CheckBox {
        id: cb
        implicitHeight: 24
        leftPadding: 0
        rightPadding: 0
        spacing: 7
        hoverEnabled: true
        focusPolicy: Qt.NoFocus

        indicator: Rectangle {
            width: 16
            height: 16
            x: cb.leftPadding
            y: (cb.height - height) / 2
            radius: Math.min(theme.controlRadius, 4)
            color: cb.checked
                ? (cb.hovered ? theme.accent : theme.accentFill)
                : (cb.hovered ? theme.controlHover : theme.control)
            border.width: cb.checked ? 0 : 1
            border.color: cb.hovered ? theme.accent : theme.controlBorder
            Behavior on color { ColorAnimation { duration: 90 } }

            Canvas {
                visible: cb.checked
                anchors.fill: parent
                property color checkColor: theme.accentText
                onCheckColorChanged: requestPaint()
                onVisibleChanged: if (visible) requestPaint()
                onWidthChanged: if (visible) requestPaint()
                onHeightChanged: if (visible) requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const c = checkColor
                    ctx.strokeStyle = "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")"
                    ctx.lineWidth = 1.8
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(4.0, 8.2)
                    ctx.lineTo(6.8, 11.0)
                    ctx.lineTo(12.2, 5.2)
                    ctx.stroke()
                }
            }
        }

        contentItem: Text {
            text: cb.text
            color: theme.text
            font.pixelSize: theme.fontSizeBody
            leftPadding: cb.indicator.width + cb.spacing
            verticalAlignment: Text.AlignVCenter
        }

        background: Item {}
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 16
        anchors.bottomMargin: 14
        spacing: 12

        // Top Hero Card
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: heroCol.implicitHeight + 28
            color: theme.surface
            radius: theme.cardRadius
            border.width: 1
            border.color: theme.softBorder

            ColumnLayout {
                id: heroCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 14
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: "当前互联网出口路由"
                        color: theme.text
                        font.pixelSize: theme.fontSizeTitle
                        font.weight: Font.Bold
                    }

                    BadgePill {
                        labelText: controller.isElevated ? "管理员权限" : "标准权限（只读）"
                        bgColor: controller.isElevated ? theme.successBg : theme.warningBg
                        borderColor: controller.isElevated ? theme.successBorder : theme.warningBorder
                        textColor: controller.isElevated ? theme.successText : theme.warningText
                        hasBorder: true
                        boldText: true
                    }

                    BadgePill {
                        visible: controller.hasTunTakeover
                        labelText: controller.tunTakeoverBadge
                        bgColor: theme.infoBg
                        borderColor: theme.infoBorder
                        textColor: theme.infoText
                        hasBorder: true
                        boldText: true
                    }

                    Item { Layout.fillWidth: true }

                    ActionBtn {
                        text: "刷新 (F5)"
                        enabled: !controller.busy
                        onClicked: controller.refresh()
                    }
                    ActionBtn {
                        text: "出口测速"
                        enabled: !controller.busy
                        onClicked: controller.testConnectivity()
                    }
                    ActionBtn {
                        text: "恢复系统自动"
                        enabled: !controller.busy && controller.internetCount > 0
                        onClicked: controller.restoreAllAutomatic()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: 54
                        color: theme.cardSurface
                        radius: Math.min(theme.cardRadius, 8)
                        border.width: 1
                        border.color: theme.softBorder

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Rectangle {
                                width: 9
                                height: 9
                                radius: theme.rounded ? 4.5 : 0
                                color: controller.activeV4Headline.indexOf("无可用") === 0 ? theme.faint : "#10B981"
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: "IPv4 默认出口"
                                    color: theme.subtext
                                    font.pixelSize: theme.fontSizeCaption
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: controller.activeV4Headline
                                    color: theme.text
                                    font.pixelSize: theme.fontSizeBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: 54
                        color: theme.cardSurface
                        radius: Math.min(theme.cardRadius, 8)
                        border.width: 1
                        border.color: theme.softBorder

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Rectangle {
                                width: 9
                                height: 9
                                radius: theme.rounded ? 4.5 : 0
                                color: controller.activeV6Headline.indexOf("未启用") === 0 ? theme.faint : theme.accent
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: "IPv6 默认出口"
                                    color: theme.subtext
                                    font.pixelSize: theme.fontSizeCaption
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: controller.activeV6Headline
                                    color: theme.text
                                    font.pixelSize: theme.fontSizeBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: controller.hasSplitEgress
                    Layout.fillWidth: true
                    implicitHeight: splitWarnTxt.implicitHeight + 14
                    color: theme.warningBg
                    radius: Math.min(theme.controlRadius, 6)
                    border.width: 1
                    border.color: theme.warningBorder

                    Text {
                        id: splitWarnTxt
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        text: controller.splitEgressWarning
                        color: theme.warningText
                        font.pixelSize: theme.fontSizeSmall
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // Main Scrollable Area
        ScrollView {
            id: mainScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }

            Item {
                width: mainScroll.availableWidth
                implicitHeight: scrollCol.implicitHeight + 4

                ColumnLayout {
                    id: scrollCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 8
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 2
                        text: "互联网出口网卡（按优先级排序，上优先下备选）"
                        color: theme.text
                        font.pixelSize: theme.fontSizeSection
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        visible: controller.internetCount === 0
                        Layout.fillWidth: true
                        implicitHeight: 54
                        color: theme.surface
                        radius: theme.cardRadius
                        border.width: 1
                        border.color: theme.softBorder

                        Text {
                            anchors.centerIn: parent
                            text: "当前未检测到具备默认网关的互联网出口网卡"
                            color: theme.subtext
                            font.pixelSize: theme.fontSizeSmall
                        }
                    }

                    Repeater {
                        model: controller.internetModel
                        delegate: Rectangle {
                            id: inetCard
                            readonly property bool primaryActive: rankIndex === 0 && isActiveV4
                            Layout.fillWidth: true
                            implicitHeight: inetRow.implicitHeight + 24
                            color: isActiveV4 ? theme.activeCardBg : theme.surface
                            radius: theme.cardRadius
                            border.width: isActiveV4 ? 1.5 : 1
                            border.color: isActiveV4 ? theme.activeCardBorder : theme.softBorder

                            RowLayout {
                                id: inetRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                anchors.topMargin: 12
                                spacing: 14

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Flow {
                                        id: inetBadgeFlow
                                        Layout.fillWidth: true
                                        spacing: 8

                                        BadgePill {
                                            labelText: rankText
                                            bgColor: rankIndex === 0 ? theme.infoBg : theme.badgeNeutralBg
                                            borderColor: rankIndex === 0 ? theme.infoBorder : theme.softBorder
                                            textColor: rankIndex === 0 ? theme.infoText : theme.badgeNeutralText
                                            hasBorder: rankIndex === 0
                                            boldText: true
                                        }

                                        BadgePill {
                                            labelText: kindText
                                            bgColor: theme.badgeNeutralBg
                                            textColor: theme.badgeNeutralText
                                            pillRadius: Math.min(theme.controlRadius, 6)
                                            horizontalPad: 7
                                        }

                                        Text {
                                            height: 22
                                            verticalAlignment: Text.AlignVCenter
                                            width: Math.min(implicitWidth, Math.max(80, inetBadgeFlow.width - 12))
                                            text: name
                                            color: theme.text
                                            font.pixelSize: 15
                                            font.weight: Font.Bold
                                            elide: Text.ElideRight
                                        }

                                        BadgePill {
                                            visible: egressBadge.length > 0
                                            labelText: egressBadge
                                            bgColor: theme.successBg
                                            borderColor: theme.successBorder
                                            textColor: theme.successText
                                            hasBorder: true
                                            boldText: true
                                        }
                                    }

                                    Text {
                                        visible: description.length > 0
                                        Layout.fillWidth: true
                                        text: description
                                        color: theme.subtext
                                        font.pixelSize: theme.fontSizeSmall
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: networkSummary
                                        color: theme.text
                                        font.pixelSize: theme.fontSizeSmall
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: metricSummary
                                        color: theme.accent
                                        font.pixelSize: theme.fontSizeSmall
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                }

                                RowLayout {
                                    Layout.alignment: Qt.AlignVCenter
                                    spacing: 5

                                    ActionBtn {
                                        text: inetCard.primaryActive ? "当前首选" : "设为首选"
                                        Layout.minimumWidth: 84
                                        primaryStyle: !inetCard.primaryActive
                                        activeSuccessStyle: inetCard.primaryActive
                                        enabled: !controller.busy && !inetCard.primaryActive
                                        onClicked: controller.setAsPrimary(ifIndex)
                                    }

                                    ActionBtn {
                                        text: "↑"
                                        implicitWidth: 32
                                        leftPadding: 0
                                        rightPadding: 0
                                        enabled: !controller.busy && !isFirst
                                        onClicked: controller.moveUp(ifIndex)
                                    }

                                    ActionBtn {
                                        text: "↓"
                                        implicitWidth: 32
                                        leftPadding: 0
                                        rightPadding: 0
                                        enabled: !controller.busy && !isLast
                                        onClicked: controller.moveDown(ifIndex)
                                    }

                                    ActionBtn {
                                        text: "自定义"
                                        enabled: !controller.busy
                                        onClicked: metricDialog.openForAdapter(
                                            ifIndex, name, ipv4InterfaceMetric, ipv4AutoMetric)
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 2
                        Layout.topMargin: 6
                        text: "局域网 / 虚拟网卡（仅本地子网，不抢外网）"
                        color: theme.text
                        font.pixelSize: theme.fontSizeSection
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        visible: controller.lanCount === 0
                        Layout.fillWidth: true
                        implicitHeight: 48
                        color: theme.surface
                        radius: theme.cardRadius
                        border.width: 1
                        border.color: theme.softBorder

                        Text {
                            anchors.centerIn: parent
                            text: "当前无已连接的局域网或虚拟隧道网卡"
                            color: theme.subtext
                            font.pixelSize: theme.fontSizeSmall
                        }
                    }

                    Repeater {
                        model: controller.lanModel
                        delegate: Rectangle {
                            readonly property bool hasActiveEgress: isActiveV4 || isActiveV6
                            Layout.fillWidth: true
                            implicitHeight: lanRow.implicitHeight + 20
                            color: theme.surface
                            radius: theme.cardRadius
                            border.width: (hasActiveEgress || isTunTakeover) ? 1.5 : 1
                            border.color: hasActiveEgress
                                ? theme.activeCardBorder
                                : (isTunTakeover ? theme.infoBorder : theme.softBorder)

                            RowLayout {
                                id: lanRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                anchors.topMargin: 10
                                spacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3

                                    Flow {
                                        id: lanBadgeFlow
                                        Layout.fillWidth: true
                                        spacing: 8

                                        BadgePill {
                                            labelText: rankText
                                            bgColor: theme.badgeNeutralBg
                                            textColor: theme.badgeNeutralText
                                            boldText: true
                                        }

                                        BadgePill {
                                            labelText: kindText
                                            bgColor: theme.badgeNeutralBg
                                            textColor: theme.badgeNeutralText
                                            pillRadius: Math.min(theme.controlRadius, 6)
                                            horizontalPad: 7
                                        }

                                        Text {
                                            height: 22
                                            verticalAlignment: Text.AlignVCenter
                                            width: Math.min(implicitWidth, Math.max(80, lanBadgeFlow.width - 12))
                                            text: name
                                            color: theme.text
                                            font.pixelSize: theme.fontSizeSection
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        BadgePill {
                                            visible: egressBadge.length > 0
                                            labelText: egressBadge
                                            bgColor: hasActiveEgress
                                                ? theme.successBg
                                                : (isTunTakeover ? theme.infoBg : theme.cardSurface)
                                            borderColor: hasActiveEgress
                                                ? theme.successBorder
                                                : (isTunTakeover ? theme.infoBorder : theme.softBorder)
                                            textColor: hasActiveEgress
                                                ? theme.successText
                                                : (isTunTakeover ? theme.infoText : theme.subtext)
                                            hasBorder: true
                                            boldText: hasActiveEgress || isTunTakeover
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: networkSummary
                                        color: theme.subtext
                                        font.pixelSize: theme.fontSizeSmall
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: metricSummary
                                        color: theme.faint
                                        font.pixelSize: theme.fontSizeCaption
                                        elide: Text.ElideRight
                                    }
                                }

                                ActionBtn {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: "自定义"
                                    enabled: !controller.busy
                                    onClicked: metricDialog.openForAdapter(
                                        ifIndex, name, ipv4InterfaceMetric, ipv4AutoMetric)
                                }
                            }
                        }
                    }

                    // Collapsible Disconnected Adapters
                    ColumnLayout {
                        visible: controller.showDisconnected
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 8

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 24

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                spacing: 6

                                Text {
                                    text: root.disconnectedExpanded ? "▾" : "▸"
                                    color: theme.subtext
                                    font.pixelSize: theme.fontSizeSmall
                                }

                                Text {
                                    text: "已断开网卡 (" + controller.disconnectedCount + ")"
                                    color: theme.subtext
                                    font.pixelSize: theme.fontSizeSection
                                    font.weight: Font.DemiBold
                                }

                                Item { Layout.fillWidth: true }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.disconnectedExpanded = !root.disconnectedExpanded
                            }
                        }

                        ColumnLayout {
                            visible: root.disconnectedExpanded
                            Layout.fillWidth: true
                            spacing: 6

                            Rectangle {
                                visible: controller.disconnectedCount === 0
                                Layout.fillWidth: true
                                implicitHeight: 42
                                color: theme.cardSurface
                                radius: Math.min(theme.cardRadius, 8)
                                border.width: 1
                                border.color: theme.softBorder

                                Text {
                                    anchors.centerIn: parent
                                    text: "当前没有已断开的网卡"
                                    color: theme.faint
                                    font.pixelSize: theme.fontSizeSmall
                                }
                            }

                            Repeater {
                                model: controller.disconnectedModel
                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: discRow.implicitHeight + 16
                                    color: theme.cardSurface
                                    radius: Math.min(theme.cardRadius, 8)
                                    border.width: 1
                                    border.color: theme.softBorder

                                    RowLayout {
                                        id: discRow
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        anchors.topMargin: 8
                                        spacing: 10

                                        BadgePill {
                                            labelText: kindText
                                            bgColor: theme.badgeNeutralBg
                                            textColor: theme.badgeNeutralText
                                            pillRadius: Math.min(theme.controlRadius, 6)
                                            horizontalPad: 7
                                        }

                                        Text {
                                            text: name
                                            color: theme.subtext
                                            font.pixelSize: theme.fontSizeBody
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            Layout.maximumWidth: 240
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: description
                                            color: theme.faint
                                            font.pixelSize: theme.fontSizeSmall
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Bottom Footer Bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: footerCol.implicitHeight + 20
            color: theme.surface
            radius: theme.cardRadius
            border.width: 1
            border.color: theme.softBorder

            ColumnLayout {
                id: footerCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                anchors.topMargin: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    StyledCheckBox {
                        text: "同步 IPv6 优先级"
                        checked: controller.syncIpv6
                        onToggled: controller.setSyncIpv6(checked)
                    }

                    StyledCheckBox {
                        text: "显示已断开网卡"
                        checked: controller.showDisconnected
                        onToggled: controller.setShowDisconnected(checked)
                    }

                    Item { Layout.fillWidth: true }

                    ActionBtn {
                        text: "网络连接 (ncpa.cpl)"
                        onClicked: controller.openNetworkConnections()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: controller.statusMessage
                        color: controller.statusError ? theme.danger : theme.subtext
                        font.pixelSize: theme.fontSizeSmall
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        visible: controller.busy
                        Layout.preferredWidth: 110
                        Layout.preferredHeight: 6
                        radius: theme.rounded ? 3 : 0
                        color: theme.cardSurface
                        clip: true

                        Rectangle {
                            width: 44
                            height: parent.height
                            radius: parent.radius
                            color: theme.accent
                            NumberAnimation on x {
                                running: root.visible && controller.busy
                                from: -44
                                to: 110
                                duration: 950
                                loops: Animation.Infinite
                            }
                        }
                    }
                }
            }
        }
    }

    MetricDialog {
        id: metricDialog
    }
}
