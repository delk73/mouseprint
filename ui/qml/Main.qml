import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1500
    height: 980
    minimumWidth: 960
    minimumHeight: 700
    title: "Mouseprint Inspection"
    color: "#101318"

    palette.window: "#101318"
    palette.windowText: "#e7edf4"
    palette.base: "#171c23"
    palette.alternateBase: "#1d242d"
    palette.text: "#e7edf4"
    palette.button: "#242d38"
    palette.buttonText: "#e7edf4"
    palette.highlight: "#2d7182"
    palette.highlightedText: "#ffffff"

    function valueOrDash(value) {
        return value === undefined || value === null || value === "" ? "-" : value
    }

    component Metric: RowLayout {
        property string label
        property string value
        spacing: 8
        Layout.fillWidth: true
        Text { text: label; color: "#81909f"; font.pixelSize: 12; Layout.fillWidth: true }
        Text { text: valueOrDash(value); color: "#e7edf4"; font.pixelSize: 13; font.bold: true }
    }

    component Panel: Rectangle {
        color: "#171c23"
        border.color: "#27333f"
        border.width: 1
        radius: 8
    }

    component TrajectoryPlot: Item {
        id: plot
        property var points: []
        property string xRole
        property string yRole
        property string title
        property color strokeColor: "#65c7d7"
        property int gapCount: {
            var count = 0
            for (var i = 0; i < points.length; ++i) {
                if (points[i][xRole] === null || points[i][xRole] === undefined ||
                    points[i][yRole] === null || points[i][yRole] === undefined) count++
            }
            return count
        }
        implicitHeight: 270
        Layout.fillWidth: true

        Text {
            text: plot.title
            color: "#cbd6df"
            font.pixelSize: 13
            font.bold: true
            anchors.left: parent.left
            anchors.top: parent.top
        }
        Text {
            text: "NULL observations are gaps; drawing is not a stored coordinate scale"
            color: "#71808e"
            font.pixelSize: 11
            anchors.right: parent.right
            anchors.top: parent.top
        }
        Canvas {
            id: canvas
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 25
            anchors.bottomMargin: 18
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = "#11171e"
                ctx.fillRect(0, 0, width, height)
                var valid = []
                var minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity
                for (var i = 0; i < plot.points.length; ++i) {
                    var p = plot.points[i]
                    var x = p[plot.xRole], y = p[plot.yRole]
                    if (x === null || x === undefined || y === null || y === undefined) continue
                    x = Number(x); y = Number(y)
                    if (!isFinite(x) || !isFinite(y)) continue
                    valid.push({ x: x, y: y, index: i })
                    minX = Math.min(minX, x); maxX = Math.max(maxX, x)
                    minY = Math.min(minY, y); maxY = Math.max(maxY, y)
                }
                var padding = 22
                var left = padding, right = width - padding, top = padding, bottom = height - padding
                var spanX = maxX - minX, spanY = maxY - minY
                if (!isFinite(spanX) || !isFinite(spanY)) {
                    ctx.fillStyle = "#71808e"
                    ctx.font = "12px sans-serif"
                    ctx.fillText("No paired coordinates available", 18, height / 2)
                    return
                }
                if (spanX === 0) spanX = 1
                if (spanY === 0) spanY = 1
                function sx(x) { return left + (x - minX) / spanX * (right - left) }
                function sy(y) { return bottom - (y - minY) / spanY * (bottom - top) }
                ctx.strokeStyle = "#293541"
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(left, top); ctx.lineTo(left, bottom); ctx.lineTo(right, bottom); ctx.stroke()
                ctx.strokeStyle = plot.strokeColor
                ctx.fillStyle = plot.strokeColor
                ctx.lineWidth = 2
                var previous = null
                for (var j = 0; j < valid.length; ++j) {
                    var point = valid[j]
                    var hasGap = previous !== null && point.index !== previous.index + 1
                    if (previous !== null && !hasGap) {
                        ctx.beginPath(); ctx.moveTo(sx(previous.x), sy(previous.y)); ctx.lineTo(sx(point.x), sy(point.y)); ctx.stroke()
                    }
                    ctx.beginPath(); ctx.arc(sx(point.x), sy(point.y), 3, 0, Math.PI * 2); ctx.fill()
                    previous = point
                }
            }
            Component.onCompleted: requestPaint()
        }
        onPointsChanged: canvas.requestPaint()
        onWidthChanged: canvas.requestPaint()
        onHeightChanged: canvas.requestPaint()
        Text {
            text: plot.gapCount > 0 ? plot.gapCount + " unavailable observation(s)" : "No unavailable observations"
            color: plot.gapCount > 0 ? "#d6a85f" : "#71808e"
            font.pixelSize: 11
            anchors.left: parent.left
            anchors.bottom: parent.bottom
        }
    }

    header: ToolBar {
        contentHeight: 52
        background: Rectangle { color: "#141a21" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            Text { text: "MOUSEPRINT"; color: "#75d0d6"; font.pixelSize: 18; font.bold: true }
            Text { text: "READ-ONLY INSPECTION"; color: "#71808e"; font.pixelSize: 11; Layout.leftMargin: 8 }
            Item { Layout.fillWidth: true }
            Text { text: inspection.errorMessage; color: "#e28b83"; font.pixelSize: 12; elide: Text.ElideRight }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 18
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: Math.max(window.width - 36, 900)
            spacing: 14

            Panel {
                Layout.fillWidth: true
                implicitHeight: sessionSelectorColumn.implicitHeight + 28
                ColumnLayout {
                    id: sessionSelectorColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8
                    Text { text: "Recent sessions"; color: "#f0f4f7"; font.pixelSize: 16; font.bold: true }
                    Text {
                        visible: inspection.sessions.length === 0
                        text: inspection.errorMessage !== "" ? inspection.errorMessage : inspection.emptyMessage
                        color: inspection.errorMessage !== "" ? "#e28b83" : "#8b9aa8"
                    }
                    Rectangle {
                        visible: inspection.sessions.length > 0
                        Layout.fillWidth: true
                        implicitHeight: Math.min(190, Math.max(38, sessionList.contentHeight))
                        color: "#11171e"
                        border.color: "#27333f"
                        ListView {
                            id: sessionList
                            anchors.fill: parent
                            clip: true
                            model: inspection.sessions
                            spacing: 1
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: sessionList.width
                                height: 36
                                color: index === inspection.selectedSessionIndex ? "#23424d" :
                                       (index % 2 === 0 ? "#1a2129" : "#171c23")
                                border.color: index === inspection.selectedSessionIndex ? "#65c7d7" : "#26313c"
                                Row {
                                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 18
                                    Text { text: "Session " + modelData.sessionId; color: "#75d0d6"; width: 105; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { text: modelData.startText; color: "#b5c1cc"; width: 205; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { text: modelData.durationText; color: "#b5c1cc"; width: 95; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { text: "MOTION " + modelData.rawMotionCount; color: "#b5c1cc"; width: 105; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { text: "EPISODES " + modelData.episodeCount; color: "#b5c1cc"; width: 110; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                }
                                MouseArea { anchors.fill: parent; onClicked: inspection.selectSession(index) }
                            }
                        }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                implicitHeight: summaryColumn.implicitHeight + 28
                ColumnLayout {
                    id: summaryColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8
                    Text { text: "Selected session"; color: "#f0f4f7"; font.pixelSize: 16; font.bold: true }
                    Text {
                        visible: !inspection.hasRun
                        text: inspection.errorMessage !== "" ? inspection.errorMessage : inspection.emptyMessage
                        color: inspection.errorMessage !== "" ? "#e28b83" : "#8b9aa8"
                    }
                    Flow {
                        visible: inspection.hasRun
                        Layout.fillWidth: true
                        spacing: 18
                        Text { text: "Session " + valueOrDash(inspection.selectedSession.sessionId); color: "#75d0d6"; font.pixelSize: 15; font.bold: true }
                        Text { text: "Collector run " + valueOrDash(inspection.selectedSession.runId); color: "#cbd6df"; font.pixelSize: 15 }
                    }
                    GridLayout {
                        visible: inspection.hasRun
                        columns: 4
                        columnSpacing: 24
                        rowSpacing: 7
                        Layout.fillWidth: true
                        Metric { label: "Run id"; value: inspection.selectedSession.runId }
                        Metric { label: "Start"; value: inspection.selectedSession.startText }
                        Metric { label: "End"; value: inspection.selectedSession.endText }
                        Metric { label: "Duration"; value: inspection.selectedSession.durationText }
                        Metric { label: "Raw MOTION"; value: inspection.selectedSession.rawMotionCount }
                        Metric { label: "Episodes"; value: inspection.selectedSession.episodeCount }
                        Metric { label: "matched"; value: inspection.selectedSession.matched }
                        Metric { label: "unmatched_context_error"; value: inspection.selectedSession.unmatchedContextError }
                        Metric { label: "unmatched_outside_tolerance"; value: inspection.selectedSession.unmatchedOutsideTolerance }
                        Metric { label: "unmatched_no_context"; value: inspection.selectedSession.unmatchedNoContext }
                    }
                    Flow {
                        visible: inspection.hasRun
                        Layout.fillWidth: true
                        spacing: 18
                        Text { text: "device_metric_status"; color: "#81909f"; font.pixelSize: 12 }
                        Repeater {
                            model: inspection.selectedSession.deviceMetricStatusCounts
                            delegate: Text { required property var modelData; text: modelData.status + ": " + modelData.count; color: "#9bb4bf"; font.pixelSize: 12 }
                        }
                        Text { text: "compositor_metric_status"; color: "#81909f"; font.pixelSize: 12; Layout.leftMargin: 10 }
                        Repeater {
                            model: inspection.selectedSession.compositorMetricStatusCounts
                            delegate: Text { required property var modelData; text: modelData.status + ": " + modelData.count; color: "#9bb4bf"; font.pixelSize: 12 }
                        }
                    }
                    Flow {
                        visible: inspection.hasRun
                        Layout.fillWidth: true
                        spacing: 18
                        Text { text: "Compositor-space path sum (available episodes): " + valueOrDash(inspection.selectedSession.compositorPathSum); color: "#b5c1cc" }
                        Text { text: "available / unavailable: " + inspection.selectedSession.compositorPathAvailableCount + " / " + inspection.selectedSession.compositorPathUnavailableCount; color: "#9bb4bf" }
                        Text { text: "Device directional reversals total: " + valueOrDash(inspection.selectedSession.directionalReversalTotal); color: "#b5c1cc" }
                        Text { text: "available / unavailable: " + inspection.selectedSession.directionalReversalAvailableCount + " / " + inspection.selectedSession.directionalReversalUnavailableCount; color: "#9bb4bf" }
                    }
                    Text {
                        visible: inspection.hasRun
                        text: "Compositor-space path sum excludes unavailable episodes and does not imply uninterrupted cursor travel."
                        color: "#71808e"
                        font.pixelSize: 11
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                implicitHeight: deviceColumn.implicitHeight + 28
                ColumnLayout {
                    id: deviceColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8
                    Text { text: "Devices in selected session"; color: "#f0f4f7"; font.pixelSize: 16; font.bold: true }
                    Text {
                        visible: inspection.hasRun && inspection.selectedSessionDevices.length === 0
                        text: "No device evidence is available for this session."
                        color: "#8b9aa8"
                    }
                    Rectangle {
                        visible: inspection.selectedSessionDevices.length > 0
                        Layout.fillWidth: true
                        implicitHeight: Math.min(180, Math.max(38, deviceList.contentHeight + 25))
                        color: "#11171e"
                        border.color: "#27333f"
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            Rectangle {
                                Layout.fillWidth: true
                                height: 25
                                color: "#202934"
                                Row {
                                    anchors.fill: parent; anchors.leftMargin: 8; spacing: 0
                                    Repeater {
                                        model: ["DEVICE", "MOTION", "EPISODES", "DEVICE-SPACE PATH SUM", "DEVICE PATH A/U", "COMPOSITOR-SPACE PATH SUM", "COMP PATH A/U"]
                                        Text { required property string modelData; required property int index; width: [180,80,85,180,110,210,110][index]; text: modelData; color: "#71808e"; font.pixelSize: 10; font.bold: true; height: parent.height; verticalAlignment: Text.AlignVCenter }
                                    }
                                }
                            }
                            ListView {
                                id: deviceList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: inspection.selectedSessionDevices
                                spacing: 1
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: deviceList.width; height: 34
                                    color: index % 2 === 0 ? "#1a2129" : "#171c23"
                                    Row {
                                        anchors.fill: parent; anchors.leftMargin: 8; spacing: 0
                                        Text { width: 180; text: modelData.deviceName + " (" + modelData.deviceId + ")"; color: "#dce5ec"; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                        Text { width: 80; text: modelData.rawMotionCount; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                        Text { width: 85; text: modelData.episodeCount; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                        Text { width: 180; text: modelData.devicePathSum; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                        Text { width: 110; text: modelData.devicePathAvailableCount + " / " + modelData.devicePathUnavailableCount; color: "#9bb4bf"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                        Text { width: 210; text: modelData.compositorPathSum; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                        Text { width: 110; text: modelData.compositorPathAvailableCount + " / " + modelData.compositorPathUnavailableCount; color: "#9bb4bf"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    }
                                }
                            }
                        }
                    }
                    Text { visible: inspection.selectedSessionDevices.length > 0; text: "A/U = available / unavailable path values; sums are not physical distance."; color: "#71808e"; font.pixelSize: 11 }
                }
            }

            Panel {
                Layout.fillWidth: true
                implicitHeight: 230
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text { text: "Movement episodes"; color: "#f0f4f7"; font.pixelSize: 16; font.bold: true }
                    Text {
                        visible: inspection.hasRun && inspection.episodes.length === 0
                        text: "No movement episodes are available for the selected session."
                        color: "#8b9aa8"
                    }
                    Rectangle {
                        visible: inspection.episodes.length > 0
                        Layout.fillWidth: true; height: 24; color: "#202934"
                        Row {
                            anchors.fill: parent; anchors.leftMargin: 8; spacing: 0
                            Repeater {
                                model: ["ID", "DEVICE", "START", "DURATION", "END", "MOTION", "DEVICE PATH", "COMP PATH", "DISPLACEMENT", "EFFICIENCY", "REVERSALS", "DEVICE STATUS", "COMP STATUS"]
                                Text { required property string modelData; required property int index; width: [70,185,110,90,95,75,100,105,105,90,75,150,150][index]; text: modelData; color: "#71808e"; font.pixelSize: 10; font.bold: true; height: parent.height; verticalAlignment: Text.AlignVCenter }
                            }
                        }
                    }
                    ScrollView {
                        visible: inspection.episodes.length > 0
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        contentWidth: 1420
                        ListView {
                            id: episodeList
                            width: 1420
                            model: inspection.episodes
                            spacing: 1
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: episodeList.width; height: 34
                                color: index === inspection.selectedEpisodeIndex ? "#23424d" :
                                       (index % 2 === 0 ? "#1a2129" : "#171c23")
                                border.color: index === inspection.selectedEpisodeIndex ? "#65c7d7" : "#26313c"
                                Row {
                                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                                    Text { width: 70; text: modelData.episodeId; color: "#75d0d6"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 185; text: modelData.device; color: "#dce5ec"; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 110; text: modelData.startText; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 90; text: modelData.durationText; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 95; text: modelData.endReason; color: "#d6a85f"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 75; text: modelData.motionCount; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 100; text: modelData.devicePath; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 105; text: modelData.compositorPath; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 105; text: modelData.displacement; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 90; text: modelData.efficiency; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 75; text: modelData.reversals; color: "#b5c1cc"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 150; text: modelData.deviceStatus; color: "#9bb4bf"; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    Text { width: 150; text: modelData.compositorStatus; color: "#9bb4bf"; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                }
                                MouseArea { anchors.fill: parent; onClicked: inspection.selectEpisode(index) }
                            }
                        }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                implicitHeight: 360
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text { text: inspection.selectedEpisode.episodeId !== undefined ? "Selected episode " + inspection.selectedEpisode.episodeId : "No episode selected"; color: "#f0f4f7"; font.pixelSize: 16; font.bold: true }
                    Text {
                        visible: inspection.selectedEpisode.episodeId === undefined
                        text: "Select a session with movement episodes to inspect trajectory details."
                        color: "#8b9aa8"
                    }
                    Flow {
                        visible: inspection.selectedEpisode.episodeId !== undefined
                        Layout.fillWidth: true
                        spacing: 18
                        Text { text: "Device: " + valueOrDash(inspection.selectedEpisode.device); color: "#b5c1cc" }
                        Text { text: "Time: " + valueOrDash(inspection.selectedEpisode.startText) + " → " + valueOrDash(inspection.selectedEpisode.endText); color: "#b5c1cc" }
                        Text { text: "Duration: " + valueOrDash(inspection.selectedEpisode.durationText); color: "#b5c1cc" }
                        Text { text: "End: " + valueOrDash(inspection.selectedEpisode.endReason); color: "#d6a85f" }
                        Text { text: "Device status: " + valueOrDash(inspection.selectedEpisode.deviceStatus); color: "#9bb4bf" }
                        Text { text: "Compositor status: " + valueOrDash(inspection.selectedEpisode.compositorStatus); color: "#9bb4bf" }
                    }
                    RowLayout {
                        visible: inspection.selectedEpisode.episodeId !== undefined
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                        TrajectoryPlot { points: inspection.trajectoryPoints; xRole: "deviceX"; yRole: "deviceY"; title: "Device trajectory — cumulative unaccelerated libinput units"; strokeColor: "#65c7d7" }
                        TrajectoryPlot { points: inspection.trajectoryPoints; xRole: "compositorX"; yRole: "compositorY"; title: "Compositor trajectory — Hyprland screen-space coordinates"; strokeColor: "#d6a85f" }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                implicitHeight: Math.max(130, provenanceList.contentHeight + 48)
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text { text: "Trajectory provenance"; color: "#f0f4f7"; font.pixelSize: 16; font.bold: true }
                    Text {
                        visible: inspection.trajectoryPoints.length === 0
                        text: "No trajectory provenance is available for the selected episode."
                        color: "#8b9aa8"
                    }
                    ScrollView {
                        visible: inspection.trajectoryPoints.length > 0
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        ListView {
                            id: provenanceList
                            width: parent.width
                            model: inspection.trajectoryPoints
                            delegate: Row {
                                required property var modelData
                                width: provenanceList.width; height: 25; spacing: 18
                                Text { text: "ordinal " + modelData.ordinal; color: "#75d0d6"; width: 95 }
                                Text { text: "raw " + modelData.rawEventId; color: "#c2ced8"; width: 100 }
                                Text { text: "match " + valueOrDash(modelData.matchId); color: "#c2ced8"; width: 120 }
                                Text { text: "context " + valueOrDash(modelData.contextId); color: "#c2ced8"; width: 120 }
                                Text { text: "match status " + valueOrDash(modelData.matchStatus); color: "#9bb4bf"; width: 210; elide: Text.ElideRight }
                                Text { text: "context status " + valueOrDash(modelData.contextStatus); color: "#9bb4bf"; elide: Text.ElideRight }
                            }
                        }
                    }
                }
            }
        }
    }
}
