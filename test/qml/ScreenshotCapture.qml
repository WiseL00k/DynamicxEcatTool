import QtQuick

QtObject {
    property url outputDirectory
    property string lastPath: ""
    property bool lastSuccess: false
    property int revision: 0

    function localDirectory() {
        let path = decodeURIComponent(outputDirectory.toString())
        if (path.indexOf("file:///") === 0)
            path = path.substring(7)
        if (Qt.platform.os === "windows" && /^\/[A-Za-z]:\//.test(path))
            path = path.substring(1)
        return path.replace(/\/$/, "")
    }

    function save(item, name) {
        item.grabToImage(function(result) {
            lastPath = localDirectory() + "/" + name + ".png"
            lastSuccess = result.saveToFile(lastPath)
            revision++
        })
    }
}
