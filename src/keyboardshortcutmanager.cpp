#include "keyboardshortcutmanager.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QSettings>
#include <utility>

namespace {
constexpr auto settingsGroup = "keyboardShortcuts/v1";
bool overlaps(const QString &first, const QString &second)
{
    return first == second || first == "global" || second == "global";
}
bool modifierKey(int key)
{
    return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
        || key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_unknown;
}
}

KeyboardShortcutManager::KeyboardShortcutManager(QObject *parent) : QObject(parent)
{
    auto add = [this](const char *id, const char *group, const char *en, const char *tr,
                      QStringList defaults = {}, const char *scope = "browser") {
        bool valid;
        defaults = normalized(defaults, &valid);
        Q_ASSERT(valid);
        m_catalog.append(QVariantMap{{"id", id}, {"group", group}, {"titleEn", en},
            {"titleTr", QString::fromUtf8(tr)}, {"defaults", defaults}, {"scope", scope}});
        m_bindings.insert(QString::fromLatin1(id), defaults);
    };
    auto standard = [](QKeySequence::StandardKey key) {
        QStringList result;
        for (const auto &sequence : QKeySequence::keyBindings(key))
            result.append(sequence.toString(QKeySequence::PortableText));
        return result;
    };
    add("quit", "app", "Quit Lurviko", "Lurviko'ı kapat", standard(QKeySequence::Quit), "global");
    add("fullscreen", "view", "Toggle fullscreen", "Tam ekranı aç / kapat", {"F11"}, "global");
    add("select_all", "selection", "Select all", "Tümünü seç", standard(QKeySequence::SelectAll));
    add("clear_selection", "selection", "Clear selection", "Seçimi temizle", {"Ctrl+Shift+A"});
    add("copy", "files", "Copy", "Kopyala", standard(QKeySequence::Copy));
    add("cut", "files", "Cut", "Kes", {"Ctrl+X"});
    add("paste", "files", "Paste", "Yapıştır", standard(QKeySequence::Paste));
    add("paste_into", "files", "Paste into selected folder", "Seçili klasöre yapıştır");
    add("undo", "files", "Undo last file operation", "Son dosya işlemini geri al", standard(QKeySequence::Undo));
    add("duplicate", "files", "Duplicate", "Kopyasını oluştur", {"Ctrl+D"});
    add("rename", "files", "Rename / batch rename", "Yeniden / toplu adlandır", {"F2"});
    add("trash", "files", "Move to Trash", "Çöpe taşı", {"Delete"});
    add("delete", "files", "Delete permanently", "Kalıcı olarak sil", {"Shift+Delete"});
    add("open", "files", "Open selected item", "Seçili öğeyi aç", {"Return"});
    add("properties", "files", "Properties", "Özellikler", {"Alt+Return"});
    add("new_folder", "files", "Create folder", "Klasör oluştur", {"Ctrl+Shift+N"});
    add("new_file", "files", "Create file", "Dosya oluştur", {"Ctrl+N"});
    add("symlink", "files", "Create Symlink", "Symlink Oluştur");
    add("hardlink", "files", "Create Hardlink", "Hardlink Oluştur");
    add("show_target", "navigation", "Show link target", "Bağlantı hedefini göster");
    add("containing_folder", "navigation", "Open containing folder", "Öğenin yolunu aç");
    add("terminal", "navigation", "Open terminal here", "Burada terminal aç", {"Shift+F4"});
    add("refresh", "navigation", "Refresh", "Yenile", {"F5", "Ctrl+R"});
    add("find", "navigation", "Find in current location", "Bu konumda ara", standard(QKeySequence::Find));
    add("kfind", "navigation", "Open KFind here", "Burada KFind aç", {"Ctrl+Shift+F"});
    add("location", "navigation", "Edit location", "Adresi düzenle", {"Ctrl+L", "F6"});
    add("up", "navigation", "Parent folder", "Üst dizin", {"Alt+Up"});
    add("home", "navigation", "Home folder", "Ev dizini", {"Alt+Home"});
    add("back", "navigation", "Back", "Geri", {"Alt+Left", "Backspace"});
    add("forward", "navigation", "Forward", "İleri", {"Alt+Right"});
    add("new_tab", "tabs", "New tab", "Yeni sekme", {"Ctrl+T"});
    add("close_tab", "tabs", "Close tab", "Sekmeyi kapat", {"Ctrl+W"});
    add("reopen_tab", "tabs", "Reopen closed tab", "Kapatılan sekmeyi aç", {"Ctrl+Shift+T"});
    add("next_tab", "tabs", "Next tab", "Sonraki sekme", {"Ctrl+Tab", "Ctrl+PgDown"});
    add("previous_tab", "tabs", "Previous tab", "Önceki sekme", {"Ctrl+Shift+Tab", "Ctrl+PgUp"});
    add("hidden", "view", "Show hidden files", "Gizli dosyaları göster", {"Ctrl+H"});
    add("split", "view", "Toggle split view", "Bölünmüş görünüm", {"F3"});
    add("grid", "view", "Icon view", "Simge görünümü", {"Ctrl+1"});
    add("list", "view", "List view", "Liste görünümü", {"Ctrl+2"});
    add("zoom_in", "view", "Increase icon size", "Simgeleri büyüt", standard(QKeySequence::ZoomIn));
    add("zoom_out", "view", "Decrease icon size", "Simgeleri küçült", standard(QKeySequence::ZoomOut));
    add("zoom_reset", "view", "Reset icon size", "Simge boyutunu sıfırla", {"Ctrl+0"});
    add("toggle_selection", "selection", "Toggle selected item", "Öğenin seçimini değiştir", {"Space"});
    add("move_left", "selection", "Move selection left", "Seçimi sola taşı", {"Left"});
    add("move_right", "selection", "Move selection right", "Seçimi sağa taşı", {"Right"});
    add("move_up", "selection", "Move selection up", "Seçimi yukarı taşı", {"Up"});
    add("move_down", "selection", "Move selection down", "Seçimi aşağı taşı", {"Down"});
    add("extend_left", "selection", "Extend selection left", "Seçimi sola genişlet", {"Shift+Left"});
    add("extend_right", "selection", "Extend selection right", "Seçimi sağa genişlet", {"Shift+Right"});
    add("extend_up", "selection", "Extend selection up", "Seçimi yukarı genişlet", {"Shift+Up"});
    add("extend_down", "selection", "Extend selection down", "Seçimi aşağı genişlet", {"Shift+Down"});
    add("first", "selection", "Select first item", "İlk öğeyi seç", {"Home"});
    add("last", "selection", "Select last item", "Son öğeyi seç", {"End"});
    add("extend_first", "selection", "Extend selection to first item", "Seçimi ilk öğeye genişlet", {"Shift+Home"});
    add("extend_last", "selection", "Extend selection to last item", "Seçimi son öğeye genişlet", {"Shift+End"});
    add("cancel", "navigation", "Cancel current action", "Mevcut eylemi iptal et", {"Escape"});
    add("video_close", "video", "Exit fullscreen / close video", "Tam ekrandan çık / videoyu kapat", {"Escape"}, "video");
    add("video_play", "video", "Play / pause video", "Videoyu oynat / duraklat", {"Space"}, "video");
    add("video_back", "video", "Seek video backward", "Videoda geri git", {"Left"}, "video");
    add("video_forward", "video", "Seek video forward", "Videoda ileri git", {"Right"}, "video");
    add("video_volume_up", "video", "Increase video volume", "Video sesini artır", {"Up"}, "video");
    add("video_volume_down", "video", "Decrease video volume", "Video sesini azalt", {"Down"}, "video");
    add("video_mute", "video", "Mute video", "Video sesini kapat", {"M"}, "video");
    add("photo_close", "photos", "Close photo", "Görseli kapat", {"Escape"}, "photo");
    add("photo_previous", "photos", "Previous photo", "Önceki görsel", {"Left"}, "photo");
    add("photo_next", "photos", "Next photo", "Sonraki görsel", {"Right"}, "photo");
    add("photo_slideshow", "photos", "Toggle slideshow", "Slayt gösterisini aç / kapat", {"Space"}, "photo");
    add("photo_zoom_in", "photos", "Zoom into photo", "Görsele yakınlaş", {"+"}, "photo");
    add("photo_zoom_out", "photos", "Zoom out of photo", "Görselden uzaklaş", {"-"}, "photo");
    add("photo_reset", "photos", "Fit photo to window", "Görseli pencereye sığdır", {"0"}, "photo");

    QSettings settings;
    settings.beginGroup(QString::fromLatin1(settingsGroup));
    for (const auto &entry : std::as_const(m_catalog)) {
        const QString id = entry.toMap().value("id").toString();
        if (settings.contains(id)) {
            bool ok;
            const auto sequences = normalized(settings.value(id).toStringList(), &ok);
            if (ok) m_bindings[id] = sequences;
        }
    }
    // Ignore damaged/colliding external settings instead of creating ambiguous shortcuts.
    for (const auto &entry : std::as_const(m_catalog)) {
        const QString id = entry.toMap().value("id").toString();
        if (!validateBindings(id, m_bindings.value(id).toStringList()).value("ok").toBool())
            m_bindings[id] = QStringList{};
    }
    if (qApp) qApp->installEventFilter(this);
}

QVariantMap KeyboardShortcutManager::actionInfo(const QString &id) const
{
    for (const auto &entry : m_catalog)
        if (entry.toMap().value("id").toString() == id) return entry.toMap();
    return {};
}

QStringList KeyboardShortcutManager::normalized(const QStringList &sequences, bool *ok) const
{
    *ok = sequences.size() <= 8;
    QStringList result;
    for (const auto &text : sequences) {
        const QKeySequence sequence(text, QKeySequence::PortableText);
        if (sequence.count() != 1 || modifierKey(sequence[0].key())) { *ok = false; return {}; }
        const QString canonical = sequence.toString(QKeySequence::PortableText);
        if (!result.contains(canonical)) result.append(canonical);
    }
    return result;
}

QVariantMap KeyboardShortcutManager::validateBindings(const QString &id, const QStringList &sequences) const
{
    const auto action = actionInfo(id);
    if (action.isEmpty()) return {{"ok", false}, {"error", "unknown"}};
    bool valid;
    const auto canonical = normalized(sequences, &valid);
    if (!valid) return {{"ok", false}, {"error", "invalid"}};
    QVariantList conflicts;
    for (const auto &entry : m_catalog) {
        auto other = entry.toMap();
        const QString otherId = other.value("id").toString();
        if (otherId == id || !overlaps(action.value("scope").toString(), other.value("scope").toString())) continue;
        QStringList collided;
        for (const auto &sequence : m_bindings.value(otherId).toStringList())
            if (canonical.contains(sequence)) collided.append(sequence);
        if (!collided.isEmpty()) { other["sequences"] = collided; conflicts.append(other); }
    }
    return {{"ok", conflicts.isEmpty()}, {"error", conflicts.isEmpty() ? "" : "conflict"},
            {"conflicts", conflicts}, {"sequences", canonical}};
}

void KeyboardShortcutManager::persist(const QVariantMap &bindings)
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(settingsGroup));
    for (const auto &entry : m_catalog) {
        const auto action = entry.toMap();
        const QString id = action.value("id").toString();
        const auto value = bindings.value(id).toStringList();
        if (value == action.value("defaults").toStringList()) settings.remove(id);
        else settings.setValue(id, value);
    }
    settings.sync();
}

QVariantMap KeyboardShortcutManager::assign(const QString &id, const QStringList &sequences, bool replaceConflicts)
{
    const auto validation = validateBindings(id, sequences);
    if (!validation.value("ok").toBool()
        && (!replaceConflicts || validation.value("error").toString() != "conflict")) return validation;
    QVariantMap next = m_bindings;
    const auto canonical = validation.value("sequences").toStringList();
    for (const auto &entry : validation.value("conflicts").toList()) {
        const QString otherId = entry.toMap().value("id").toString();
        auto remaining = next.value(otherId).toStringList();
        for (const auto &sequence : canonical) remaining.removeAll(sequence);
        next[otherId] = remaining;
    }
    next[id] = canonical;
    if (next != m_bindings) { persist(next); m_bindings = next; emit bindingsChanged(); }
    return {{"ok", true}};
}

void KeyboardShortcutManager::resetAll()
{
    for (const auto &entry : m_catalog) {
        const auto action = entry.toMap();
        m_bindings[action.value("id").toString()] = action.value("defaults");
    }
    persist(m_bindings);
    emit bindingsChanged();
}

QString KeyboardShortcutManager::displaySequence(const QString &sequence) const
{
    return QKeySequence(sequence, QKeySequence::PortableText).toString(QKeySequence::PortableText);
}

QString KeyboardShortcutManager::sequenceForKey(int key, int modifiers)
{
    if (modifierKey(key)) return {};
    const auto flags = Qt::KeyboardModifiers(modifiers)
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    return QKeySequence(QKeyCombination(flags, static_cast<Qt::Key>(key))).toString(QKeySequence::PortableText);
}

bool KeyboardShortcutManager::matches(const QString &action, int key, int modifiers) const
{
    return !m_editorOpen && m_bindings.value(action).toStringList().contains(sequenceForKey(key, modifiers));
}

void KeyboardShortcutManager::setEditorOpen(bool open)
{
    if (open == m_editorOpen) return;
    m_editorOpen = open;
    if (!open) stopRecording();
    emit editorOpenChanged();
}
void KeyboardShortcutManager::startRecording()
{
    if (!m_editorOpen || m_recording) return;
    m_recording = true; emit recordingChanged();
}
void KeyboardShortcutManager::stopRecording()
{
    if (!m_recording) return;
    m_recording = false; emit recordingChanged();
}

bool KeyboardShortcutManager::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_editorOpen || !m_recording
        || (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride))
        return QObject::eventFilter(watched, event);
    auto *key = static_cast<QKeyEvent *>(event);
    key->accept();
    if (event->type() == QEvent::ShortcutOverride || key->isAutoRepeat()) return true;
    const auto sequence = sequenceForKey(key->key(), key->modifiers());
    if (!sequence.isEmpty()) { stopRecording(); emit sequenceRecorded(sequence); }
    return true;
}
