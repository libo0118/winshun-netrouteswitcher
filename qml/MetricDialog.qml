import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    focus: true
    padding: 0
    anchors.centerIn: Overlay.overlay
    width: Math.min(410, Overlay.overlay ? Overlay.overlay.width - 36 : 410)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property int targetIfIndex: 0
    property string adapterName: ""
    property int currentMetric: 10
    property bool isAutoMetric: true
    property string errorText: ""

    function openForAdapter(ifIndex, name, metric, isAuto) {
        targetIfIndex = ifIndex
        adapterName = name
        currentMetric = metric
        isAutoMetric = isAuto
        errorText = ""
        metricInput.text = metric > 0 ? String(metric) : "10"
        open()
    }

    function submit() {
        const raw = metricInput.text.trim()
        if (!/^\d+$/.test(raw)) {
            errorText = "请输入 1 到 9999 之间的整数。"
            metricInput.forceActiveFocus()
            return
        }
        const val = parseInt(raw, 10)
        if (isNaN(val) || val < 1 || val > 9999) {
            errorText = "请输入 1 到 9999 之间的整数。"
            metricInput.forceActiveFocus()
            return
        }
        controller.setCustomMetric(targetIfIndex, val)
        close()
    }

    onOpened: {
        controller.setDialogOpen(true)
        metricInput.forceActiveFocus()
        metricInput.selectAll()
    }
    onClosed: controller.setDialogOpen(false)

    Overlay.modal: Rectangle {
        color: theme.withAlpha("#000000", theme.dark ? 0.52 : 0.28)
    }

    background: Rectangle {
        color: theme.surface
        radius: theme.cardRadius
        border.width: 1
        border.color: theme.windowEdge
    }

    component DialogBtn: Button {
        id: dbtn
        property bool primaryStyle: false
        implicitHeight: 32
        leftPadding: 14
        rightPadding: 14
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        contentItem: Text {
            text: dbtn.text
            color: dbtn.primaryStyle ? theme.accentText : theme.text
            font.pixelSize: theme.fontSizeSmall
            font.weight: dbtn.primaryStyle ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: dbtn.primaryStyle
                ? (dbtn.down ? theme.pressed : (dbtn.hovered ? theme.accent : theme.accentFill))
                : (dbtn.down ? theme.pressed : (dbtn.hovered ? theme.controlHover : theme.control))
            radius: theme.controlRadius
            border.width: dbtn.primaryStyle ? 0 : 1
            border.color: theme.controlBorder
            Behavior on color { ColorAnimation { duration: 90 } }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 46

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 12

                Text {
                    text: "自定义接口跃点数"
                    color: theme.text
                    font.pixelSize: theme.fontSizeSection
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                Button {
                    id: closeDlgBtn
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 0
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: root.close()
                    contentItem: Canvas {
                        property color strokeColor: closeDlgBtn.hovered ? "#FFFFFF" : theme.subtext
                        onStrokeColorChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const c = strokeColor
                            ctx.strokeStyle = "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")"
                            ctx.lineWidth = 1.4
                            ctx.lineCap = "round"
                            const cx = width / 2
                            const cy = height / 2
                            const r = 4.2
                            ctx.beginPath()
                            ctx.moveTo(cx - r, cy - r)
                            ctx.lineTo(cx + r, cy + r)
                            ctx.moveTo(cx + r, cy - r)
                            ctx.lineTo(cx - r, cy + r)
                            ctx.stroke()
                        }
                    }
                    background: Rectangle {
                        color: closeDlgBtn.hovered ? theme.danger : "transparent"
                        radius: Math.min(theme.controlRadius, 6)
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.softBorder
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: "网卡：" + root.adapterName + "（当前: " + root.currentMetric + " / " + (root.isAutoMetric ? "自动" : "手动") + "）"
                color: theme.text
                font.pixelSize: theme.fontSizeBody
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                text: "跃点数越小越优先（建议范围 1 ~ 9999）。输入后将同时停用该接口的“自动跃点”。"
                color: theme.subtext
                font.pixelSize: theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: "接口跃点数 (InterfaceMetric)："
                color: theme.text
                font.pixelSize: theme.fontSizeSmall
                font.weight: Font.Medium
            }

            TextField {
                id: metricInput
                Layout.fillWidth: true
                implicitHeight: 34
                leftPadding: 10
                rightPadding: 10
                maximumLength: 4
                color: theme.text
                placeholderText: "1 ~ 9999"
                placeholderTextColor: theme.faint
                selectedTextColor: theme.accentText
                selectionColor: theme.accentFill
                font.pixelSize: theme.fontSizeSection
                inputMethodHints: Qt.ImhDigitsOnly
                onTextChanged: root.errorText = ""
                Keys.onReturnPressed: root.submit()
                Keys.onEnterPressed: root.submit()
                background: Rectangle {
                    color: theme.control
                    radius: theme.controlRadius
                    border.width: 1
                    border.color: metricInput.activeFocus ? theme.inputFocused : theme.controlBorder
                }
            }

            Text {
                visible: root.errorText.length > 0
                Layout.fillWidth: true
                text: root.errorText
                color: theme.danger
                font.pixelSize: theme.fontSizeCaption
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 10
                spacing: 8

                DialogBtn {
                    text: "恢复自动跃点"
                    onClicked: {
                        controller.restoreAdapterAuto(root.targetIfIndex)
                        root.close()
                    }
                }

                Item { Layout.fillWidth: true }

                DialogBtn {
                    text: "取消"
                    onClicked: root.close()
                }

                DialogBtn {
                    text: "确定"
                    primaryStyle: true
                    onClicked: root.submit()
                }
            }
        }
    }
}
