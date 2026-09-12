import QtQuick

QtObject {
    id: root

    property string themeMode: "system"
    property bool systemDark: false
    property string fontFamily: ""

    readonly property bool dark: themeMode === "dark"
                                 || (themeMode === "system" && systemDark)

    readonly property color pageBackground: dark ? "#0b1220" : "#f3f6fa"
    readonly property color surface: dark ? "#121c2b" : "#ffffff"
    readonly property color surfaceRaised: dark ? "#182538" : "#ffffff"
    readonly property color surfaceMuted: dark ? "#1a2738" : "#f7f9fc"
    readonly property color inputBackground: dark ? "#0d1725" : "#ffffff"
    readonly property color controlBackground: dark ? "#223247" : "#f5f7fa"
    readonly property color controlHover: dark ? "#2b3d54" : "#edf2f7"

    readonly property color border: dark ? "#2b3b50" : "#dfe5ec"
    readonly property color borderStrong: dark ? "#40536d" : "#c9d2dc"
    readonly property color divider: dark ? "#243247" : "#e9edf2"

    readonly property color textPrimary: dark ? "#eef3f8" : "#172033"
    readonly property color textSecondary: dark ? "#bdc8d7" : "#465469"
    readonly property color textMuted: dark ? "#8492a6" : "#718096"
    readonly property color textDisabled: dark ? "#58677b" : "#9aa5b1"

    readonly property color selectedBackground: dark ? "#143554" : "#e4f1ff"
    readonly property color selectedBorder: dark ? "#3178b5" : "#8bc2f5"
    readonly property color accent: dark ? "#58a6ff" : "#1769aa"
    readonly property color accentHover: dark ? "#79b8ff" : "#12578f"
    readonly property color accentPressed: dark ? "#388bfd" : "#0f4776"
    readonly property color accentText: dark ? "#9dceff" : "#12578f"
    readonly property color textOnAccent: dark ? "#07111f" : "#ffffff"

    readonly property color success: dark ? "#45d483" : "#218653"
    readonly property color successText: dark ? "#87efb4" : "#176b40"
    readonly property color successBackground: dark ? "#113624" : "#e9f8f0"
    readonly property color successBorder: dark ? "#2c6d49" : "#95d5b2"

    readonly property color warning: dark ? "#f5b942" : "#b36b00"
    readonly property color warningText: dark ? "#ffd98a" : "#875000"
    readonly property color warningBackground: dark ? "#3b2b0e" : "#fff6df"
    readonly property color warningBorder: dark ? "#7a5a22" : "#edc877"

    readonly property color danger: dark ? "#ff7474" : "#c83f49"
    readonly property color dangerHover: dark ? "#ff9292" : "#ac3039"
    readonly property color dangerPressed: dark ? "#f05252" : "#902831"
    readonly property color dangerText: dark ? "#ffaaaa" : "#a92f38"
    readonly property color dangerBackground: dark ? "#3a171b" : "#fff0f1"
    readonly property color dangerBorder: dark ? "#803139" : "#e7a0a5"

    readonly property int space2: 2
    readonly property int space4: 4
    readonly property int space6: 6
    readonly property int space8: 8
    readonly property int space10: 10
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space20: 20
    readonly property int space24: 24
    readonly property int space32: 32

    readonly property int radiusSmall: 5
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12

    readonly property int controlHeightSmall: 30
    readonly property int controlHeight: 36
    readonly property int controlHeightLarge: 42
    readonly property int topBarHeight: 56
    readonly property int navigationExpandedWidth: 208
    readonly property int navigationCollapsedWidth: 68
    readonly property int logDockCollapsedHeight: 40
    readonly property int logDockExpandedHeight: 190

    readonly property int fontCaption: 11
    readonly property int fontBodySmall: 12
    readonly property int fontBody: 13
    readonly property int fontSubtitle: 15
    readonly property int fontTitle: 20
    readonly property int fontDisplay: 24

    readonly property int animationFast: 100
    readonly property int animationNormal: 180
}
