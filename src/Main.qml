import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win
    width: 980
    height: 700
    minimumWidth: 820
    minimumHeight: 580
    visible: true
    title: "Omaimage"
    color: page

    readonly property color page: backend.themeBackground
    readonly property color ink: backend.themeForeground
    readonly property color accent: backend.themeAccent
    readonly property color muted: backend.themeMuted
    readonly property color raised: backend.themeRaised
    readonly property color selection: backend.themeSelection
    readonly property color danger: backend.themeDanger
    readonly property string fontFamily: backend.fontFamily
    property int step: 0
    property string writeMessage: ""
    property bool writeFailed: false

    Material.theme: backend.darkMode ? Material.Dark : Material.Light
    Material.accent: accent
    Material.foreground: ink
    Material.background: page
    font.family: fontFamily
    font.pixelSize: Math.round(15 * backend.textScale)

    function mix(base, tint, amount) {
        return Qt.rgba(base.r + (tint.r - base.r) * amount,
                       base.g + (tint.g - base.g) * amount,
                       base.b + (tint.b - base.b) * amount, 1)
    }

    QtObject {
        id: setup
        property string hostname: ""
        property string username: ""
        property string password: ""
        property string password2: ""
        property string locale: ""
        property string timezone: ""
        property string keyboard: ""
        property bool wifiEnabled: false
        property string wifiSsid: ""
        property string wifiPassword: ""
        property string wifiCountry: ""
        property bool wifiHidden: false
        property bool wifiOpen: false
        property bool sshEnabled: false
        property bool sshPasswordAuth: true
        property string sshKeys: ""
        property bool passwordlessSudo: false
        property bool enableI2C: false
        property bool enableSPI: false
        property bool enable1Wire: false
        property bool enableUsbGadget: false
        property string serial: "Disabled"
        property bool connectEnabled: false
        property string connectToken: ""
    }

    component Line: TextField {
        Layout.fillWidth: true
        color: win.ink
        placeholderTextColor: win.muted
        font.family: win.fontFamily
        font.pixelSize: Math.round(15 * backend.textScale)
        background: Rectangle {
            radius: 6
            color: win.raised
            border.width: 1
            border.color: win.mix(win.page, win.ink, 0.18)
        }
    }

    component Action: Button {
        property bool primary: false
        font.family: win.fontFamily
        font.capitalization: Font.MixedCase
        implicitHeight: 40
        leftPadding: 16
        rightPadding: 16
        contentItem: Text {
            text: parent.text
            color: parent.primary ? win.page : win.ink
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: parent.font
        }
        background: Rectangle {
            radius: 6
            color: parent.primary ? win.accent : win.mix(win.page, win.ink, parent.hovered ? 0.14 : 0.08)
        }
    }

    component ThemedBox: ComboBox {
        id: box
        font.family: win.fontFamily
        font.pixelSize: Math.round(15 * backend.textScale)
        palette.text: win.ink
        palette.windowText: win.ink
        palette.buttonText: win.ink
        palette.window: win.raised
        palette.base: win.raised
        palette.highlight: win.selection
        palette.highlightedText: win.ink
        contentItem: Text {
            leftPadding: 12
            rightPadding: box.indicator.width + 8
            text: box.displayText
            color: box.enabled ? win.ink : win.muted
            font: box.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: win.raised
            border.width: 1
            border.color: win.mix(win.page, win.ink, 0.18)
        }
        delegate: Item {
            id: option
            required property var model
            required property int index
            width: box.width
            height: 36
            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: 4
                color: box.highlightedIndex === option.index ? win.selection : "transparent"
            }
            Text {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                verticalAlignment: Text.AlignVCenter
                text: box.textRole ? option.model[box.textRole] : option.model.modelData
                color: win.ink
                font.family: win.fontFamily
                font.pixelSize: Math.round(14 * backend.textScale)
                elide: Text.ElideRight
            }
        }
        popup: Popup {
            y: box.height + 4
            width: box.width
            implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
            padding: 4
            background: Rectangle {
                color: win.raised
                radius: 6
                border.width: 1
                border.color: win.mix(win.page, win.ink, 0.18)
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: box.popup.visible ? box.delegateModel : null
                currentIndex: box.highlightedIndex
                ScrollBar.vertical: ScrollBar {
                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: 3
                        color: win.mix(win.page, win.ink, 0.4)
                    }
                }
            }
        }
    }

    component ThemedScroll: ScrollBar {
        policy: ScrollBar.AsNeeded
        contentItem: Rectangle {
            implicitWidth: 6
            radius: 3
            color: win.mix(win.page, win.ink, 0.4)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "Omaimage"
                color: win.ink
                font.pixelSize: 22
                font.family: win.fontFamily
            }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [qsTr("Image"), qsTr("Drive"), qsTr("Setup"), qsTr("Write")]
                Action {
                    text: modelData
                    enabled: !backend.busy && (index === 0
                             || (index >= 1 && backend.hasSelection)
                             || (index >= 2 && backend.driveIndex >= 0))
                    primary: win.step === index
                    onClicked: win.step = index
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: win.step

            ColumnLayout {
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Action {
                        text: qsTr("Back")
                        visible: backend.canGoBack
                        onClicked: {
                            if (searchField.text !== "")
                                searchField.text = ""
                            else
                                backend.goBack()
                        }
                    }
                    Text {
                        text: backend.crumb
                        color: win.muted
                        font.family: win.fontFamily
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    ThemedBox {
                        id: deviceFilter
                        Layout.preferredWidth: 220
                        model: backend.deviceFilters
                        textRole: "name"
                        currentIndex: backend.deviceFilterIndex
                        onActivated: backend.deviceFilterIndex = currentIndex
                    }
                    Action {
                        text: qsTr("Refresh")
                        onClicked: backend.refreshCatalog()
                    }
                    Action {
                        text: qsTr("Sources")
                        onClicked: sources.open()
                    }
                    Action {
                        text: qsTr("Local file")
                        onClicked: backend.chooseLocalImage()
                    }
                }
                Line {
                    id: searchField
                    placeholderText: qsTr("Search")
                    onTextChanged: backend.setSearch(text)
                }
                Text {
                    text: backend.status
                    color: win.muted
                    font.pixelSize: 12
                    font.family: win.fontFamily
                }
                ListView {
                    id: imageList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: backend.entries
                    spacing: 6
                    ScrollBar.vertical: ThemedScroll { }
                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property var modelData
                        readonly property bool chosen: !modelData.category && backend.selection.url === modelData.url
                        readonly property string iconSource: {
                            var icon = String(modelData.icon || "")
                            if (icon.indexOf("https://") === 0 || icon.indexOf("http://") === 0
                                    || icon.indexOf("data:") === 0 || icon.indexOf("file:") === 0)
                                return icon
                            return ""
                        }
                        width: imageList.width
                        height: modelData.description || modelData.meta ? 76 : 56
                        radius: 6
                        color: chosen ? win.selection : (hover.hovered ? win.mix(win.raised, win.ink, 0.1) : win.raised)
                        HoverHandler { id: hover }
                        Image {
                            id: thumb
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: 36
                            height: 36
                            source: row.iconSource
                            visible: row.iconSource !== ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                        Column {
                            anchors.left: thumb.visible ? thumb.right : parent.left
                            anchors.leftMargin: 12
                            anchors.right: chevron.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                width: parent.width
                                text: row.modelData.name || ""
                                color: win.ink
                                font.family: win.fontFamily
                                font.pixelSize: 15
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: row.modelData.description || ""
                                color: win.muted
                                font.family: win.fontFamily
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                visible: text !== ""
                            }
                            Text {
                                width: parent.width
                                text: row.modelData.meta || ""
                                color: win.muted
                                font.family: win.fontFamily
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                visible: text !== ""
                            }
                        }
                        Text {
                            id: chevron
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.category ? "›" : ""
                            color: win.ink
                            font.pixelSize: 22
                            font.family: win.fontFamily
                        }
                        TapHandler { onTapped: backend.openEntry(row.index) }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("No images. Check the connection or add a source.")
                        color: win.muted
                        visible: imageList.count === 0
                        font.family: win.fontFamily
                    }
                }
            }

            ColumnLayout {
                spacing: 10
                RowLayout {
                    Text {
                        text: qsTr("USB drive")
                        color: win.ink
                        font.pixelSize: 18
                        font.family: win.fontFamily
                    }
                    Item { Layout.fillWidth: true }
                    CheckBox {
                        id: showInternal
                        text: qsTr("Internal drives")
                        font.family: win.fontFamily
                        onToggled: backend.refreshDrives(checked)
                    }
                    Action {
                        text: qsTr("Refresh")
                        onClicked: backend.refreshDrives(showInternal.checked)
                    }
                }
                Text {
                    text: qsTr("The system disk is never shown. Everything on the drive will be replaced.")
                    color: win.muted
                    font.family: win.fontFamily
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ListView {
                    id: driveList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: backend.drives
                    spacing: 6
                    ScrollBar.vertical: ThemedScroll { }
                    delegate: Rectangle {
                        id: driveRow
                        required property int index
                        required property var modelData
                        width: driveList.width
                        height: 64
                        radius: 6
                        color: backend.driveIndex === index ? win.selection
                               : (driveHover.hovered ? win.mix(win.raised, win.ink, 0.1) : win.raised)
                        HoverHandler { id: driveHover }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 14
                            spacing: 4
                            Text {
                                text: driveRow.modelData.title || ""
                                color: win.ink
                                font.family: win.fontFamily
                                width: parent.width
                                elide: Text.ElideRight
                            }
                            Text {
                                text: driveRow.modelData.detail || ""
                                color: win.muted
                                font.family: win.fontFamily
                                font.pixelSize: 12
                                width: parent.width
                                elide: Text.ElideRight
                            }
                        }
                        TapHandler { onTapped: backend.selectDrive(driveRow.index) }
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("No USB drive found. Plug one in and refresh the list.")
                        color: win.muted
                        visible: driveList.count === 0
                        font.family: win.fontFamily
                    }
                }
            }

            ScrollView {
                id: settingsScroll
                clip: true
                ColumnLayout {
                    width: settingsScroll.availableWidth
                    spacing: 12

                    Text {
                        text: backend.hasSelection && backend.selection.customisation
                              ? qsTr("Omaimage writes these settings to the boot partition. The Pi starts without the setup wizard.")
                              : qsTr("This image has no Raspberry Pi first-boot setup. It is only written to the drive.")
                        color: win.muted
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        font.family: win.fontFamily
                        font.pixelSize: 13
                    }
                    Text {
                        visible: backend.hasSelection
                        text: qsTr("Format: %1").arg(backend.selection.formatLabel || "")
                        color: win.ink
                        font.family: win.fontFamily
                    }
                    ThemedBox {
                        visible: backend.hasSelection && backend.selection.formatEditable
                        Layout.fillWidth: true
                        font.family: win.fontFamily
                        model: [
                            { label: qsTr("No first-boot setup"), value: "none" },
                            { label: qsTr("Raspberry Pi OS (cloud-init)"), value: "cloudinit-rpi" },
                            { label: qsTr("cloud-init"), value: "cloudinit" },
                            { label: qsTr("Raspberry Pi OS Legacy (firstrun)"), value: "systemd" },
                            { label: qsTr("rpi-preseed"), value: "rpi-preseed" }
                        ]
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: {
                            var value = backend.selection.initFormat || "none"
                            for (var i = 0; i < model.length; ++i) {
                                if (model[i].value === value)
                                    return i
                            }
                            return 0
                        }
                        onActivated: backend.setInitFormat(currentValue)
                    }

                    ColumnLayout {
                        visible: backend.hasSelection && backend.selection.customisation
                        Layout.fillWidth: true
                        spacing: 12

                        Text { text: qsTr("User"); color: win.ink; font.bold: true; font.family: win.fontFamily }
                        Text {
                            text: qsTr("A name and password skip the wizard on first boot.")
                            color: win.muted
                            font.pixelSize: 12
                            font.family: win.fontFamily
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Line { placeholderText: qsTr("Username"); text: setup.username; onTextEdited: setup.username = text }
                        Line { placeholderText: qsTr("Password"); echoMode: TextInput.Password; text: setup.password; onTextEdited: setup.password = text }
                        Line { placeholderText: qsTr("Repeat password"); echoMode: TextInput.Password; text: setup.password2; onTextEdited: setup.password2 = text }
                        CheckBox {
                            text: qsTr("sudo without a password")
                            font.family: win.fontFamily
                            checked: setup.passwordlessSudo
                            onToggled: setup.passwordlessSudo = checked
                        }

                        Text { text: qsTr("Wi-Fi"); color: win.ink; font.bold: true; font.family: win.fontFamily }
                        CheckBox {
                            text: qsTr("Set up Wi-Fi")
                            font.family: win.fontFamily
                            checked: setup.wifiEnabled
                            onToggled: setup.wifiEnabled = checked
                        }
                        Line { enabled: setup.wifiEnabled; placeholderText: qsTr("Network name"); text: setup.wifiSsid; onTextEdited: setup.wifiSsid = text }
                        Line {
                            enabled: setup.wifiEnabled && !setup.wifiOpen
                            placeholderText: qsTr("Wi-Fi password")
                            echoMode: TextInput.Password
                            text: setup.wifiPassword
                            onTextEdited: setup.wifiPassword = text
                        }
                        ThemedBox {
                            id: countryBox
                            enabled: setup.wifiEnabled
                            Layout.fillWidth: true
                            font.family: win.fontFamily
                            model: ["DE", "AT", "CH", "NL", "BE", "FR", "IT", "ES", "PT", "PL", "CZ", "DK", "SE", "NO", "FI", "GB", "IE", "US", "CA", "AU", "NZ", "JP"]
                            onActivated: setup.wifiCountry = currentText
                        }
                        CheckBox {
                            enabled: setup.wifiEnabled
                            text: qsTr("Hidden network")
                            font.family: win.fontFamily
                            checked: setup.wifiHidden
                            onToggled: setup.wifiHidden = checked
                        }
                        CheckBox {
                            enabled: setup.wifiEnabled
                            text: qsTr("Open network")
                            font.family: win.fontFamily
                            checked: setup.wifiOpen
                            onToggled: setup.wifiOpen = checked
                        }

                        Text { text: qsTr("Region"); color: win.ink; font.bold: true; font.family: win.fontFamily }
                        Line { placeholderText: qsTr("Locale, for example en_US.UTF-8"); text: setup.locale; onTextEdited: setup.locale = text }
                        Line { placeholderText: qsTr("Time zone, for example Europe/Berlin"); text: setup.timezone; onTextEdited: setup.timezone = text }
                        ThemedBox {
                            id: keyboardBox
                            Layout.fillWidth: true
                            font.family: win.fontFamily
                            model: ["de", "us", "gb", "fr", "ch", "at", "nl", "es", "it", "pt", "pl", "se", "dk", "no", "fi", "cz", "hu", "tr", "ru", "jp"]
                            onActivated: setup.keyboard = currentText
                        }

                        Text { text: qsTr("SSH"); color: win.ink; font.bold: true; font.family: win.fontFamily }
                        CheckBox {
                            text: qsTr("Enable SSH")
                            font.family: win.fontFamily
                            checked: setup.sshEnabled
                            onToggled: setup.sshEnabled = checked
                        }
                        CheckBox {
                            enabled: setup.sshEnabled
                            text: qsTr("Password login")
                            font.family: win.fontFamily
                            checked: setup.sshPasswordAuth
                            onToggled: setup.sshPasswordAuth = checked
                        }
                        Line {
                            enabled: setup.sshEnabled
                            placeholderText: qsTr("Public keys, one per line")
                            text: setup.sshKeys
                            onTextEdited: setup.sshKeys = text
                        }
                        Action {
                            enabled: setup.sshEnabled
                            text: qsTr("Load key file")
                            onClicked: {
                                var keys = backend.pickSshKeys()
                                if (keys !== "")
                                    setup.sshKeys = setup.sshKeys === "" ? keys : setup.sshKeys + "\n" + keys
                            }
                        }

                        Text { text: qsTr("Hostname"); color: win.ink; font.bold: true; font.family: win.fontFamily }
                        Line { placeholderText: qsTr("Hostname, empty keeps raspberrypi"); text: setup.hostname; onTextEdited: setup.hostname = text }

                        ColumnLayout {
                            visible: backend.selection.interfaces
                            Layout.fillWidth: true
                            spacing: 8
                            Text { text: qsTr("Interfaces"); color: win.ink; font.bold: true; font.family: win.fontFamily }
                            CheckBox { text: "I2C"; font.family: win.fontFamily; checked: setup.enableI2C; onToggled: setup.enableI2C = checked }
                            CheckBox { text: "SPI"; font.family: win.fontFamily; checked: setup.enableSPI; onToggled: setup.enableSPI = checked }
                            CheckBox { text: "1-Wire"; font.family: win.fontFamily; checked: setup.enable1Wire; onToggled: setup.enable1Wire = checked }
                            CheckBox { text: "USB-Gadget"; font.family: win.fontFamily; checked: setup.enableUsbGadget; onToggled: setup.enableUsbGadget = checked }
                            ThemedBox {
                                id: serialBox
                                Layout.fillWidth: true
                                font.family: win.fontFamily
                                model: [
                                    { label: qsTr("Serial off"), value: "Disabled" },
                                    { label: qsTr("Serial default"), value: "Default" },
                                    { label: qsTr("Serial console"), value: "Console" },
                                    { label: qsTr("Serial hardware"), value: "Hardware" },
                                    { label: qsTr("Console and hardware"), value: "Console & Hardware" }
                                ]
                                textRole: "label"
                                valueRole: "value"
                                onActivated: setup.serial = currentValue
                            }
                        }

                        Text { text: "Raspberry Pi Connect"; color: win.ink; font.bold: true; font.family: win.fontFamily }
                        CheckBox {
                            text: qsTr("Enable Connect")
                            font.family: win.fontFamily
                            checked: setup.connectEnabled
                            onToggled: setup.connectEnabled = checked
                        }
                        Line {
                            enabled: setup.connectEnabled
                            placeholderText: qsTr("Connect token, optional")
                            echoMode: TextInput.Password
                            text: setup.connectToken
                            onTextEdited: setup.connectToken = text
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 10
                Text {
                    text: qsTr("Write")
                    color: win.ink
                    font.pixelSize: 18
                    font.family: win.fontFamily
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: win.ink
                    font.family: win.fontFamily
                    text: (backend.selection.name || qsTr("No image")) + "\n"
                          + (backend.driveIndex >= 0 && backend.drives[backend.driveIndex]
                             ? backend.drives[backend.driveIndex].title + "  ·  " + backend.drives[backend.driveIndex].detail
                             : qsTr("No drive"))
                }
                Text {
                    id: sizeWarning
                    visible: tooSmall
                    text: qsTr("The image does not fit on the drive.")
                    color: win.danger
                    font.family: win.fontFamily
                    property bool tooSmall: {
                        var image = Number(backend.selection.extractSize || 0)
                        var drive = backend.driveIndex >= 0 && backend.drives[backend.driveIndex]
                                    ? Number(backend.drives[backend.driveIndex].size || 0) : 0
                        return image > 0 && drive > 0 && image > drive
                    }
                }
                CheckBox {
                    id: confirm
                    text: qsTr("I understand that the drive will be completely overwritten")
                    font.family: win.fontFamily
                    enabled: !backend.busy
                }
                Action {
                    text: backend.busy ? qsTr("Cancel") : qsTr("Overwrite drive")
                    primary: !backend.busy
                    enabled: backend.busy || (confirm.checked && backend.hasSelection && backend.driveIndex >= 0 && !sizeWarning.tooSmall)
                    onClicked: {
                        if (backend.busy) {
                            backend.cancelWrite()
                            return
                        }
                        win.writeMessage = ""
                        win.writeFailed = false
                        backend.startWrite({
                            hostname: setup.hostname,
                            username: setup.username,
                            password: setup.password,
                            password2: setup.password2,
                            locale: setup.locale,
                            timezone: setup.timezone,
                            keyboard: setup.keyboard,
                            wifiEnabled: setup.wifiEnabled,
                            wifiSsid: setup.wifiSsid,
                            wifiPassword: setup.wifiPassword,
                            wifiCountry: setup.wifiCountry,
                            wifiHidden: setup.wifiHidden,
                            wifiOpen: setup.wifiOpen,
                            sshEnabled: setup.sshEnabled,
                            sshPasswordAuth: setup.sshPasswordAuth,
                            sshKeys: setup.sshKeys,
                            passwordlessSudo: setup.passwordlessSudo,
                            enableI2C: setup.enableI2C,
                            enableSPI: setup.enableSPI,
                            enable1Wire: setup.enable1Wire,
                            enableUsbGadget: setup.enableUsbGadget,
                            serial: setup.serial,
                            connectEnabled: setup.connectEnabled,
                            connectToken: setup.connectToken
                        })
                    }
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    indeterminate: backend.busy && backend.progress < 0
                    value: backend.progress < 0 ? 0 : backend.progress
                    visible: backend.busy || backend.progressLabel !== ""
                }
                Text {
                    text: backend.progressLabel
                    color: win.muted
                    font.family: win.fontFamily
                    visible: text !== ""
                }
                Text {
                    text: win.writeMessage
                    color: win.writeFailed ? win.danger : win.ink
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    font.family: win.fontFamily
                    visible: text !== ""
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        text: backend.logText
                        readOnly: true
                        wrapMode: Text.Wrap
                        color: win.ink
                        font.family: win.fontFamily
                        font.pixelSize: 13
                        background: Rectangle { color: win.raised; radius: 6 }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: win.step < 3
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: win.muted
                font.family: win.fontFamily
                text: backend.hasSelection ? qsTr("Selected: %1").arg(backend.selection.name) : qsTr("No image selected yet")
            }
            Action {
                text: qsTr("Next")
                primary: true
                enabled: !backend.busy && ((win.step === 0 && backend.hasSelection)
                         || (win.step === 1 && backend.driveIndex >= 0)
                         || win.step === 2)
                onClicked: win.step = Math.min(3, win.step + 1)
            }
        }
    }

    Popup {
        id: sources
        modal: true
        focus: true
        width: Math.min(560, win.width - 48)
        height: Math.min(520, win.height - 48)
        x: (win.width - width) / 2
        y: (win.height - height) / 2
        padding: 18
        background: Rectangle { color: win.raised; radius: 8; border.color: win.mix(win.page, win.ink, 0.16) }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Text { text: qsTr("Sources"); color: win.ink; font.pixelSize: 18; font.family: win.fontFamily }
            Text {
                text: qsTr("Catalogs in the Raspberry Pi Imager format, or single download URLs.")
                color: win.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                font.family: win.fontFamily
                font.pixelSize: 12
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                clip: true
                model: backend.repositories
                spacing: 4
                delegate: RowLayout {
                    id: repoRow
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: 6
                        color: win.mix(win.page, win.ink, 0.06)
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: Text.AlignVCenter
                            text: repoRow.modelData.title || ""
                            color: win.ink
                            elide: Text.ElideRight
                            font.family: win.fontFamily
                        }
                    }
                    Action { text: qsTr("Remove"); onClicked: backend.removeRepository(repoRow.modelData.url) }
                }
            }
            Line { id: repoUrl; placeholderText: "https://…/os_list.json" }
            Action {
                text: qsTr("Add catalog")
                onClicked: {
                    backend.addRepository(repoUrl.text)
                    repoUrl.text = ""
                }
            }
            Text { text: qsTr("Single images"); color: win.ink; font.family: win.fontFamily }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.directImages
                spacing: 4
                delegate: RowLayout {
                    id: directRow
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: 6
                        color: win.mix(win.page, win.ink, 0.06)
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: Text.AlignVCenter
                            text: directRow.modelData.name || ""
                            color: win.ink
                            elide: Text.ElideRight
                            font.family: win.fontFamily
                        }
                    }
                    Action { text: qsTr("Remove"); onClicked: backend.removeDirectImage(directRow.modelData.url) }
                }
            }
            Line { id: directName; placeholderText: qsTr("Name") }
            Line { id: directUrl; placeholderText: "https://…/bild.img.xz" }
            Action {
                text: qsTr("Add image URL")
                onClicked: {
                    backend.addDirectImage(directName.text, directUrl.text, "none")
                    directName.text = ""
                    directUrl.text = ""
                }
            }
            Action { text: qsTr("Close"); onClicked: sources.close() }
        }
    }

    Timer {
        interval: 2000
        repeat: true
        running: win.step === 1 && !backend.busy
        onTriggered: backend.refreshDrives(showInternal.checked)
    }

    Connections {
        target: backend
        function onFinished(message) {
            win.writeMessage = message
            win.writeFailed = false
            confirm.checked = false
        }
        function onFailed(message) {
            win.writeMessage = message
            win.writeFailed = true
        }
    }

    Shortcut {
        sequence: "Ctrl+Q"
        context: Qt.ApplicationShortcut
        onActivated: win.close()
    }

    property rect normalGeometry: Qt.rect(x, y, width, height)
    property bool wasMaximized: false
    function trackNormalGeometry() {
        if (visibility === Window.Windowed)
            normalGeometry = Qt.rect(x, y, width, height)
    }
    onXChanged: trackNormalGeometry()
    onYChanged: trackNormalGeometry()
    onWidthChanged: trackNormalGeometry()
    onHeightChanged: trackNormalGeometry()
    onVisibilityChanged: {
        if (visibility === Window.Maximized || visibility === Window.FullScreen)
            wasMaximized = true
        else if (visibility === Window.Windowed)
            wasMaximized = false
    }
    onStepChanged: {
        if (step === 1)
            backend.refreshDrives(showInternal.checked)
    }

    Component.onCompleted: {
        var saved = backend.savedSetup()
        setup.hostname = saved.hostname || ""
        setup.username = saved.username || ""
        setup.locale = saved.locale || backend.defaultLocale()
        setup.timezone = saved.timezone || backend.defaultTimezone()
        setup.keyboard = saved.keyboard || backend.defaultKeyboard()
        setup.wifiCountry = saved.wifiCountry || backend.defaultCountry()
        setup.wifiSsid = saved.wifiSsid || ""
        setup.wifiEnabled = saved.wifiEnabled === true
        setup.wifiHidden = saved.wifiHidden === true
        setup.wifiOpen = saved.wifiOpen === true
        setup.sshEnabled = saved.sshEnabled === true
        setup.sshPasswordAuth = saved.sshPasswordAuth !== false
        setup.sshKeys = saved.sshKeys || ""
        setup.passwordlessSudo = saved.passwordlessSudo === true
        setup.enableI2C = saved.enableI2C === true
        setup.enableSPI = saved.enableSPI === true
        setup.enable1Wire = saved.enable1Wire === true
        setup.enableUsbGadget = saved.enableUsbGadget === true
        setup.serial = saved.serial || "Disabled"
        setup.connectEnabled = saved.connectEnabled === true
        var keyboardIndex = keyboardBox.model.indexOf(setup.keyboard)
        if (keyboardIndex >= 0)
            keyboardBox.currentIndex = keyboardIndex
        var countryIndex = countryBox.model.indexOf(setup.wifiCountry)
        if (countryIndex >= 0)
            countryBox.currentIndex = countryIndex

        var geometry = backend.windowGeometry()
        if (geometry.valid) {
            x = geometry.x
            y = geometry.y
            width = geometry.width
            height = geometry.height
            if (geometry.maximized)
                showMaximized()
        }
    }
    Component.onDestruction: backend.saveWindowGeometry(
        normalGeometry.x, normalGeometry.y, normalGeometry.width, normalGeometry.height, wasMaximized)
}
