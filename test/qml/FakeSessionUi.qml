import QtQuick

QtObject {
    property bool nicReady: true
    property bool sessionActive: false
    property string sessionMode: "空闲"
    readonly property bool idle: !sessionActive
    readonly property string modeKey: sessionMode === "DC测试" ? "dc" : "idle"
    readonly property string statusText: sessionActive ? sessionMode : "总线空闲"
    readonly property string statusTone: sessionActive ? "success" : "neutral"
}
