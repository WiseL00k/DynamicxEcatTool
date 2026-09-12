import QtQuick

QtObject {
    property QtObject dcTest: QtObject {
        id: dcTestObject
        signal errorOccurred(string message)
    }

    property var nicList: ["Ethernet adapter"]
    property bool connected: false
    property bool sessionActive: false
    property string sessionMode: "空闲"
    property int selectedNic: -1

    signal logUpdated(string line)
    signal logAppend(string line)
    signal dcLogAppended(string line)
    signal soemErrorOccurred(string message)

    function refreshNicsAsync() {}
    function changedSelectedNic(index) { selectedNic = index }

    function emitReleasedDcFailure(message) {
        dcLogAppended(message)
        dcTestObject.errorOccurred(message)
    }
}
