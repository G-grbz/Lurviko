import QtQuick
import QtQuick.Controls
import Lurviko.App

GOverlaySurface {
    id: surface
    property var popup: null
    property var colorTheme: AppTheme
    customColor: colorTheme.commandBar !== undefined ? colorTheme.commandBar : colorTheme.surface
    nativeTheme: AppTheme.useQtTheme && colorTheme === AppTheme
    nativeColor: AppTheme.desktopAppearance.menuBackground

    radius: 20
    border.width: 1
    border.color: colorTheme.borderStrong

}
