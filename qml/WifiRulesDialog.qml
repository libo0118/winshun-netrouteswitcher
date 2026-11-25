import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    focus: true
    padding: 0
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 36 : 640)
    height: Math.min(540, Overlay.overlay ? Overlay.overlay.height - 36 : 540)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property bool editorMode: false
    property string editingRuleId: ""
    property string errorText: ""
    property int adapterOrderRevision: 0

    readonly property var ssidChips: {
        const list = []
        const seen = {}
        const current = controller.connectedWifiSsid.trim()
        if (current.length > 0) {
            const k = current.toLowerCase()
            seen[k] = true
            list.push({ value: current, label: "当前: " + current, isCurrent: true })
        }
        const saved = controller.savedWifiProfiles
        for (let i = 0; i < saved.length; ++i) {
            const p = String(saved[i]).trim()
            if (p.length === 0) continue
            const k = p.toLowerCase()
            if (!seen[k]) {
                seen[k] = true
                list.push({ value: p, label: p, isCurrent: false })
            }
        }
        if (!seen["*"]) {
            list.push({ value: "*", label: "* (任意 Wi-Fi)", isCurrent: false })
        }
        return list
    }

    ListModel {
        id: adapterOrderModel
    }

    function openList() {
        editorMode = false
        errorText = ""
        open()
    }

    function populateAdapterOrderModel(savedOrder) {
        adapterOrderModel.clear()
        const seen = {}
        if (savedOrder && savedOrder.length > 0) {
            for (let i = 0; i < savedOrder.length; ++i) {
                const name = String(savedOrder[i]).trim()
                if (name.length === 0) continue
                const key = name.toLowerCase()
                if (!seen[key]) {
                    seen[key] = true
                    adapterOrderModel.append({ adapterName: name, included: true })
                }
            }
        }
        const known = controller.knownAdapterNames
        const defaultIncluded = !savedOrder || savedOrder.length === 0
        for (let i = 0; i < known.length; ++i) {
            const name = String(known[i]).trim()
            if (name.length === 0) continue
            const key = name.toLowerCase()
            if (!seen[key]) {
                seen[key] = true
                adapterOrderModel.append({ adapterName: name, included: defaultIncluded })
            }
        }
        adapterOrderRevision += 1
    }

    function openCreate(initialSsid) {
        editingRuleId = ""
        const init = initialSsid ? String(initialSsid).trim() : ""
        ssidInput.text = init
        customAdapterInput.text = ""
        ruleEnabledCheck.checked = true
        applyOnSaveCheck.checked = init.length > 0 && init === controller.connectedWifiSsid.trim()
        errorText = ""
        populateAdapterOrderModel([])
        editorMode = true
        ssidInput.forceActiveFocus()
        ssidInput.selectAll()
    }

    function openEdit(rowIndex) {
        const item = controller.wifiRuleModel.get(rowIndex)
        if (!item || !item.ruleId) {
            return
        }
        editingRuleId = String(item.ruleId)
        ssidInput.text = item.ssid ? String(item.ssid) : ""
        customAdapterInput.text = ""
        ruleEnabledCheck.checked = Boolean(item.enabled)
        applyOnSaveCheck.checked = Boolean(item.isMatched)
        errorText = ""
        populateAdapterOrderModel(item.adapterOrder)
        editorMode = true
        ssidInput.forceActiveFocus()
    }

    function checkedRankForRow(targetIndex) {
        let rank = 0
        for (let i = 0; i <= targetIndex && i < adapterOrderModel.count; ++i) {
            if (adapterOrderModel.get(i).included) {
                ++rank
            }
        }
        return rank
    }

    function moveAdapterRow(index, delta) {
        const target = index + delta
        if (index < 0 || target < 0 || target >= adapterOrderModel.count) {
            return
        }
        adapterOrderModel.move(index, target, 1)
        adapterOrderRevision += 1
    }

    function addCustomAdapter() {
        const raw = customAdapterInput.text.trim()
        if (raw.length === 0) {
            return
        }
        const key = raw.toLowerCase()
        for (let i = 0; i < adapterOrderModel.count; ++i) {
            const row = adapterOrderModel.get(i)
            if (String(row.adapterName).toLowerCase() === key) {
                adapterOrderModel.setProperty(i, "included", true)
                adapterOrderRevision += 1
                customAdapterInput.text = ""
                errorText = ""
                return
            }
        }
        adapterOrderModel.append({ adapterName: raw, included: true })
        adapterOrderRevision += 1
        customAdapterInput.text = ""
        errorText = ""
    }

    function saveRule() {
        const s = ssidInput.text.trim()
        if (s.length === 0) {
            errorText = "请输入目标 Wi-Fi 名称（支持 * 和 ? 通配符）。"
            ssidInput.forceActiveFocus()
            return
        }
        const order = []
        for (let i = 0; i < adapterOrderModel.count; ++i) {
            const row = adapterOrderModel.get(i)
            if (row.included) {
                order.push(String(row.adapterName))
            }
        }
        if (order.length === 0) {
            errorText = "请至少勾选一个出口网卡并设置优先顺序。"
            return
        }
        controller.saveWifiRule(
            editingRuleId,
            s,
            order,
            ruleEnabledCheck.checked,
            applyOnSaveCheck.checked
        )
        errorText = ""
        editorMode = false
    }

    onOpened: {
        controller.refreshWifiProfiles()
        controller.setDialogOpen(true)
    }
    onClosed: {
        editorMode = false
        errorText = ""
        controller.setDialogOpen(false)
    }

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
        property bool dangerStyle: false
        property bool compact: false
        implicitHeight: compact ? 26 : 32
        leftPadding: compact ? 8 : 14
        rightPadding: compact ? 8 : 14
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        opacity: enabled ? 1.0 : 0.45
        contentItem: Text {
            text: dbtn.text
            color: dbtn.primaryStyle
                ? theme.accentText
                : (dbtn.dangerStyle ? theme.danger : theme.text)
            font.pixelSize: dbtn.compact ? theme.fontSizeCaption : theme.fontSizeSmall
            font.weight: dbtn.primaryStyle ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: dbtn.primaryStyle
                ? (dbtn.down ? theme.pressed : (dbtn.hovered ? theme.accent : theme.accentFill))
                : (dbtn.down
                    ? theme.pressed
                    : (dbtn.hovered
                        ? (dbtn.dangerStyle ? theme.withAlpha(theme.danger, 0.14) : theme.controlHover)
                        : theme.control))
            radius: theme.controlRadius
            border.width: dbtn.primaryStyle ? 0 : 1
            border.color: dbtn.dangerStyle && dbtn.hovered ? theme.danger : theme.controlBorder
            Behavior on color { ColorAnimation { duration: 90 } }
        }
    }

    component RuleSwitch: Switch {
        id: rsw
        implicitHeight: 24
        leftPadding: 0
        rightPadding: 0
        spacing: rsw.text.length > 0 ? 8 : 0
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        indicator: Rectangle {
            implicitWidth: 36
            implicitHeight: 20
            x: rsw.leftPadding
            y: (rsw.height - height) / 2
            radius: theme.rounded ? 10 : 2
            color: rsw.checked
                ? (rsw.hovered ? theme.accent : theme.accentFill)
                : (rsw.hovered ? theme.controlHover : theme.control)
            border.width: rsw.checked ? 0 : 1
            border.color: rsw.hovered ? theme.accent : theme.controlBorder
            Behavior on color { ColorAnimation { duration: 90 } }

            Rectangle {
                width: 14
                height: 14
                y: 3
                x: rsw.checked ? parent.width - width - 3 : 3
                radius: theme.rounded ? 7 : 1
                color: rsw.checked ? theme.accentText : theme.subtext
                Behavior on x { NumberAnimation { duration: 110 } }
            }
        }
        contentItem: Text {
            visible: rsw.text.length > 0
            text: rsw.text
            color: theme.text
            font.pixelSize: theme.fontSizeBody
            font.weight: Font.DemiBold
            leftPadding: rsw.text.length > 0 ? (rsw.indicator.width + rsw.spacing) : 0
            verticalAlignment: Text.AlignVCenter
        }
        background: Item {}
    }

    component DialogCheckBox: CheckBox {
        id: dcb
        implicitHeight: 24
        leftPadding: 0
        rightPadding: 0
        spacing: 7
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        indicator: Rectangle {
            width: 16
            height: 16
            x: dcb.leftPadding
            y: (dcb.height - height) / 2
            radius: Math.min(theme.controlRadius, 4)
            color: dcb.checked
                ? (dcb.hovered ? theme.accent : theme.accentFill)
                : (dcb.hovered ? theme.controlHover : theme.control)
            border.width: dcb.checked ? 0 : 1
            border.color: dcb.hovered ? theme.accent : theme.controlBorder
            Behavior on color { ColorAnimation { duration: 90 } }
            Canvas {
                visible: dcb.checked
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
            text: dcb.text
            color: theme.text
            font.pixelSize: theme.fontSizeBody
            leftPadding: dcb.indicator.width + dcb.spacing
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Item {}
    }

    component DialogPill: Rectangle {
        id: dpill
        property string labelText: ""
        property color bgColor: theme.badgeNeutralBg
        property color borderColor: theme.softBorder
        property color textColor: theme.badgeNeutralText
        property bool hasBorder: false
        property bool boldText: false
        implicitWidth: dpillTxt.implicitWidth + 14
        implicitHeight: 20
        radius: theme.rounded ? 9 : 0
        color: bgColor
        border.width: hasBorder ? 1 : 0
        border.color: borderColor
        Text {
            id: dpillTxt
            anchors.centerIn: parent
            text: dpill.labelText
            color: dpill.textColor
            font.pixelSize: theme.fontSizeCaption
            font.weight: dpill.boldText ? Font.DemiBold : Font.Normal
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
                    text: root.editorMode
                        ? (root.editingRuleId.length > 0 ? "编辑 Wi-Fi 自动切换规则" : "新建 Wi-Fi 自动切换规则")
                        : "Wi-Fi 自动切换规则"
                    color: theme.text
                    font.pixelSize: theme.fontSizeSection
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                Button {
                    id: closeWifiDlgBtn
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 0
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: root.close()
                    contentItem: Canvas {
                        property color strokeColor: closeWifiDlgBtn.hovered ? "#FFFFFF" : theme.subtext
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
                        color: closeWifiDlgBtn.hovered ? theme.danger : "transparent"
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

        // List View
        ColumnLayout {
            visible: !root.editorMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: topCtrlRow.implicitHeight + 20
                color: theme.cardSurface
                radius: Math.min(theme.cardRadius, 8)
                border.width: 1
                border.color: theme.softBorder

                RowLayout {
                    id: topCtrlRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        RuleSwitch {
                            id: masterWifiSwitch
                            text: "按 Wi-Fi 自动切换"
                            checked: controller.autoSwitchByWifi
                            onToggled: controller.setAutoSwitchByWifi(masterWifiSwitch.checked)
                        }

                        Text {
                            Layout.fillWidth: true
                            text: controller.connectedWifiSsid.length > 0
                                ? ("当前连接 Wi-Fi：" + controller.connectedWifiSsid)
                                : "当前未连接 Wi-Fi（连接匹配 Wi-Fi 时自动生效）"
                            color: controller.connectedWifiSsid.length > 0 ? theme.accent : theme.subtext
                            font.pixelSize: theme.fontSizeSmall
                            elide: Text.ElideRight
                        }
                    }

                    DialogBtn {
                        text: "+ 新建规则"
                        onClicked: root.openCreate("")
                    }

                    DialogBtn {
                        text: "按当前 Wi-Fi 新建"
                        primaryStyle: true
                        enabled: controller.connectedWifiSsid.length > 0
                        onClicked: root.openCreate(controller.connectedWifiSsid)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: "按规则列表从上到下匹配（支持 * 和 ? 通配符，精确 SSID 请置于通配符规则之上）。"
                color: theme.subtext
                font.pixelSize: theme.fontSizeCaption
                wrapMode: Text.WordWrap
            }

            ScrollView {
                id: rulesScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }

                Item {
                    width: rulesScroll.availableWidth
                    implicitHeight: rulesCol.implicitHeight + 4

                    ColumnLayout {
                        id: rulesCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.rightMargin: 8
                        spacing: 8

                        Rectangle {
                            visible: controller.wifiRuleCount === 0
                            Layout.fillWidth: true
                            implicitHeight: 72
                            color: theme.cardSurface
                            radius: Math.min(theme.cardRadius, 8)
                            border.width: 1
                            border.color: theme.softBorder

                            Text {
                                anchors.centerIn: parent
                                width: parent.width - 24
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: "尚未配置 Wi-Fi 自动切换规则，可点击上方“+ 新建规则”添加。"
                                color: theme.subtext
                                font.pixelSize: theme.fontSizeSmall
                            }
                        }

                        Repeater {
                            model: controller.wifiRuleModel
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: ruleRow.implicitHeight + 20
                                color: model.isMatched ? theme.activeCardBg : theme.cardSurface
                                radius: Math.min(theme.cardRadius, 8)
                                border.width: model.isMatched ? 1.5 : 1
                                border.color: model.isMatched ? theme.successBorder : theme.softBorder

                                RowLayout {
                                    id: ruleRow
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 10
                                    spacing: 10

                                    RuleSwitch {
                                        id: ruleEnableSw
                                        checked: model.enabled
                                        onToggled: controller.setWifiRuleEnabled(model.ruleId, ruleEnableSw.checked)
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        opacity: model.enabled ? 1.0 : 0.60

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Text {
                                                Layout.maximumWidth: 210
                                                text: model.ssid
                                                color: theme.text
                                                font.pixelSize: theme.fontSizeBody
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }

                                            DialogPill {
                                                visible: model.isMatched
                                                labelText: "当前命中"
                                                bgColor: theme.successBg
                                                borderColor: theme.successBorder
                                                textColor: theme.successText
                                                hasBorder: true
                                                boldText: true
                                            }

                                            DialogPill {
                                                visible: !model.enabled
                                                labelText: "已停用"
                                            }

                                            Item { Layout.fillWidth: true }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: "优先顺序：" + model.orderSummary
                                            color: theme.subtext
                                            font.pixelSize: theme.fontSizeSmall
                                            elide: Text.ElideRight
                                        }
                                    }

                                    RowLayout {
                                        spacing: 4

                                        DialogBtn {
                                            compact: true
                                            text: "立即应用"
                                            enabled: !controller.busy
                                            onClicked: controller.applyWifiRuleNow(model.ruleId)
                                        }

                                        DialogBtn {
                                            compact: true
                                            text: "编辑"
                                            onClicked: root.openEdit(index)
                                        }

                                        DialogBtn {
                                            compact: true
                                            text: "↑"
                                            implicitWidth: 26
                                            enabled: index > 0
                                            onClicked: controller.moveWifiRule(model.ruleId, -1)
                                        }

                                        DialogBtn {
                                            compact: true
                                            text: "↓"
                                            implicitWidth: 26
                                            enabled: index + 1 < controller.wifiRuleCount
                                            onClicked: controller.moveWifiRule(model.ruleId, 1)
                                        }

                                        DialogBtn {
                                            compact: true
                                            dangerStyle: true
                                            text: "删除"
                                            onClicked: controller.deleteWifiRule(model.ruleId)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: controller.isElevated
                        ? "提示：规则保存于插件数据目录，支持多网卡自动按序调整跃点。"
                        : "提示：当前无管理员权限，自动规则匹配后需提权才能修改跃点。"
                    color: controller.isElevated ? theme.faint : theme.warningText
                    font.pixelSize: theme.fontSizeCaption
                    elide: Text.ElideRight
                }

                DialogBtn {
                    text: "关闭"
                    onClicked: root.close()
                }
            }
        }

        // Editor / Create View
        ColumnLayout {
            visible: root.editorMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 10

            ScrollView {
                id: editorScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }

                Item {
                    width: editorScroll.availableWidth
                    implicitHeight: editorCol.implicitHeight + 4

                    ColumnLayout {
                        id: editorCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.rightMargin: 8
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: "目标 Wi-Fi 名称 (SSID)："
                            color: theme.text
                            font.pixelSize: theme.fontSizeSmall
                            font.weight: Font.DemiBold
                        }

                        TextField {
                            id: ssidInput
                            Layout.fillWidth: true
                            implicitHeight: 34
                            leftPadding: 10
                            rightPadding: 10
                            color: theme.text
                            placeholderText: "输入 Wi-Fi SSID，支持 * / ? 通配符（如 Office-*）"
                            placeholderTextColor: theme.faint
                            selectedTextColor: theme.accentText
                            selectionColor: theme.accentFill
                            font.pixelSize: theme.fontSizeBody
                            onTextChanged: root.errorText = ""
                            background: Rectangle {
                                color: theme.control
                                radius: theme.controlRadius
                                border.width: 1
                                border.color: ssidInput.activeFocus ? theme.inputFocused : theme.controlBorder
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 6

                            Repeater {
                                model: root.ssidChips
                                delegate: Button {
                                    id: chipBtn
                                    implicitHeight: 24
                                    leftPadding: 10
                                    rightPadding: 10
                                    hoverEnabled: true
                                    focusPolicy: Qt.NoFocus
                                    onClicked: {
                                        ssidInput.text = modelData.value
                                        root.errorText = ""
                                    }
                                    contentItem: Text {
                                        text: modelData.label
                                        color: modelData.isCurrent ? theme.infoText : theme.text
                                        font.pixelSize: theme.fontSizeCaption
                                        font.weight: modelData.isCurrent ? Font.DemiBold : Font.Normal
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        radius: theme.rounded ? 12 : theme.controlRadius
                                        color: modelData.isCurrent
                                            ? (chipBtn.hovered ? theme.controlHover : theme.infoBg)
                                            : (chipBtn.hovered ? theme.controlHover : theme.control)
                                        border.width: 1
                                        border.color: modelData.isCurrent ? theme.infoBorder : theme.controlBorder
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            text: "出口网卡优先顺序（勾选参与排序的网卡，上方优先）："
                            color: theme.text
                            font.pixelSize: theme.fontSizeSmall
                            font.weight: Font.DemiBold
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Repeater {
                                model: adapterOrderModel
                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 38
                                    color: model.included ? theme.cardSurface : theme.page
                                    radius: Math.min(theme.controlRadius, 6)
                                    border.width: 1
                                    border.color: theme.softBorder

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 8
                                        spacing: 8

                                        DialogPill {
                                            labelText: (root.adapterOrderRevision >= 0 && model.included)
                                                ? ("#" + root.checkedRankForRow(index))
                                                : "-"
                                            bgColor: model.included ? theme.infoBg : theme.badgeNeutralBg
                                            borderColor: model.included ? theme.infoBorder : theme.softBorder
                                            textColor: model.included ? theme.infoText : theme.badgeNeutralText
                                            hasBorder: model.included
                                            boldText: model.included
                                        }

                                        DialogCheckBox {
                                            id: adapterCheck
                                            Layout.fillWidth: true
                                            text: model.adapterName
                                            checked: model.included
                                            onToggled: {
                                                adapterOrderModel.setProperty(index, "included", adapterCheck.checked)
                                                root.adapterOrderRevision += 1
                                                root.errorText = ""
                                            }
                                        }

                                        DialogBtn {
                                            compact: true
                                            text: "↑"
                                            implicitWidth: 26
                                            enabled: index > 0
                                            onClicked: root.moveAdapterRow(index, -1)
                                        }

                                        DialogBtn {
                                            compact: true
                                            text: "↓"
                                            implicitWidth: 26
                                            enabled: index + 1 < adapterOrderModel.count
                                            onClicked: root.moveAdapterRow(index, 1)
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            TextField {
                                id: customAdapterInput
                                Layout.fillWidth: true
                                implicitHeight: 32
                                leftPadding: 10
                                rightPadding: 10
                                color: theme.text
                                placeholderText: "添加未插线/自定义网卡名（如 以太网 2、USB 共享）"
                                placeholderTextColor: theme.faint
                                selectedTextColor: theme.accentText
                                selectionColor: theme.accentFill
                                font.pixelSize: theme.fontSizeSmall
                                Keys.onReturnPressed: root.addCustomAdapter()
                                Keys.onEnterPressed: root.addCustomAdapter()
                                background: Rectangle {
                                    color: theme.control
                                    radius: theme.controlRadius
                                    border.width: 1
                                    border.color: customAdapterInput.activeFocus ? theme.inputFocused : theme.controlBorder
                                }
                            }

                            DialogBtn {
                                text: "添加网卡"
                                enabled: customAdapterInput.text.trim().length > 0
                                onClicked: root.addCustomAdapter()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 16

                            DialogCheckBox {
                                id: ruleEnabledCheck
                                text: "启用此规则"
                                checked: true
                            }

                            DialogCheckBox {
                                id: applyOnSaveCheck
                                text: "保存并立即应用"
                                checked: true
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }

            Text {
                visible: root.errorText.length > 0
                Layout.fillWidth: true
                text: root.errorText
                color: theme.danger
                font.pixelSize: theme.fontSizeCaption
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                DialogBtn {
                    text: "取消"
                    onClicked: {
                        root.editorMode = false
                        root.errorText = ""
                    }
                }

                Item { Layout.fillWidth: true }

                DialogBtn {
                    text: "保存规则"
                    primaryStyle: true
                    onClicked: root.saveRule()
                }
            }
        }
    }
}
