#pragma once

#include <QObject>
#include <QString>

// Settings → "Use Lurviko for file dialogs".
//
// xdg-desktop-portal picks the FileChooser backend from portals.conf. A user
// file (~/.config/xdg-desktop-portal/<desktop>-portals.conf) replaces the
// system one entirely, so enabling copies the system preferences and adds
// only the FileChooser line ("lurviko;<previous>", which falls back to the
// previous backend if Lurviko's is not installed). Disabling deletes a file
// Lurviko created, or removes just that line from a file the user owns.
class FileDialogIntegration final : public QObject
{
    Q_OBJECT
    // lurviko.portal is where xdg-desktop-portal reads backend definitions
    // and the D-Bus activation file is installed.
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // "", "notInstalled", "writeFailed" or "installFailed".
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    // KDE/Qt apps open their dialogs in-process unless
    // PLASMA_INTEGRATION_USE_PORTAL=1 (plasma-integration); GTK apps likewise
    // need GTK_USE_PORTAL / GDK_DEBUG=portals. Set for the next Plasma login
    // through a plasma-workspace env script.
    Q_PROPERTY(bool plasmaSession READ plasmaSession CONSTANT)
    Q_PROPERTY(bool desktopAppsEnabled READ desktopAppsEnabled NOTIFY changed)

public:
    explicit FileDialogIntegration(QObject *parent = nullptr);

    bool installed() const { return m_installed; }
    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString configPath() const;
    bool plasmaSession() const;
    bool desktopAppsEnabled() const { return m_desktopApps; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setEnabled(bool enabled);
    // Copies lurviko.portal into the system portal directory (pkexec).
    Q_INVOKABLE void installPortalDefinition();
    Q_INVOKABLE void setDesktopAppsEnabled(bool enabled);

    static QString environmentScriptPath();
    static QString environmentScript();

    static constexpr const char *portalFileName = "lurviko.portal";
    static QString portalDefinition();
    // Pure transformation of a portals.conf text; public for tests.
    static QString updatedConfig(const QString &existing, bool enable, bool *deleteFile = nullptr);
    static QString createdHeader();

signals:
    void changed();

private:
    QString systemConfigText() const;
    void restartPortal();
    void setError(const QString &message);

    bool m_installed = false;
    bool m_enabled = false;
    bool m_busy = false;
    bool m_desktopApps = false;
    QString m_error;
};
