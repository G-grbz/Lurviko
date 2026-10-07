#include "languagemanager.h"

#include <QLocale>
#include <QList>
#include <QSettings>

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
    buildDictionary();
    QSettings settings;
    m_language = settings.value(QStringLiteral("ui/language"),
                                QLocale::system().language() == QLocale::Turkish ? QStringLiteral("tr") : QStringLiteral("en")).toString();
    if (m_language != QStringLiteral("tr") && m_language != QStringLiteral("en"))
        m_language = QStringLiteral("en");
}

void LanguageManager::setLanguage(const QString &language)
{
    const QString normalized = language.toLower();
    if ((normalized != QStringLiteral("tr") && normalized != QStringLiteral("en")) || normalized == m_language)
        return;
    m_language = normalized;
    QSettings().setValue(QStringLiteral("ui/language"), m_language);
    emit languageChanged();
}

QString LanguageManager::t(const QString &key) const
{
    const auto languageMap = m_dictionary.value(m_language);
    if (languageMap.contains(key))
        return languageMap.value(key);
    return m_dictionary.value(QStringLiteral("en")).value(key, key);
}

QString LanguageManager::localizeMessage(const QString &message) const
{
    if (m_language != QStringLiteral("tr") || message.trimmed().isEmpty())
        return message;

    static const QHash<QString, QString> exact = {
        {QStringLiteral("Folder name cannot be empty."), QStringLiteral("Klasör adı boş olamaz.")},
        {QStringLiteral("Folder created"), QStringLiteral("Klasör oluşturuldu")},
        {QStringLiteral("Folder created."), QStringLiteral("Klasör oluşturuldu.")},
        {QStringLiteral("File name cannot be empty."), QStringLiteral("Dosya adı boş olamaz.")},
        {QStringLiteral("File created"), QStringLiteral("Dosya oluşturuldu")},
        {QStringLiteral("Symbolic link created"), QStringLiteral("Sembolik bağlantı oluşturuldu")},
        {QStringLiteral("Hard link created"), QStringLiteral("Hardlink oluşturuldu")},
        {QStringLiteral("Symbolic links can currently be created only in local folders."), QStringLiteral("Sembolik bağlantılar şu anda yalnızca yerel klasörlerde oluşturulabilir.")},
        {QStringLiteral("Hard links can currently be created only in local folders."), QStringLiteral("Hardlink'ler şu anda yalnızca yerel klasörlerde oluşturulabilir.")},
        {QStringLiteral("Link name is invalid."), QStringLiteral("Bağlantı adı geçersiz.")},
        {QStringLiteral("Symbolic link target cannot be empty."), QStringLiteral("Sembolik bağlantı hedefi boş olamaz.")},
        {QStringLiteral("Hard link target cannot be empty."), QStringLiteral("Hardlink hedefi boş olamaz.")},
        {QStringLiteral("A symbolic link target must be a local path."), QStringLiteral("Sembolik bağlantı hedefi yerel bir yol olmalıdır.")},
        {QStringLiteral("A hard link target must be a local file."), QStringLiteral("Hardlink hedefi yerel bir dosya olmalıdır.")},
        {QStringLiteral("Hard link target must be an existing regular file."), QStringLiteral("Hardlink hedefi mevcut bir normal dosya olmalıdır.")},
        {QStringLiteral("An item with this name already exists."), QStringLiteral("Bu ada sahip bir öğe zaten var.")},
        {QStringLiteral("A terminal can only be opened in a local folder."), QStringLiteral("Terminal yalnızca yerel bir klasörde açılabilir.")},
        {QStringLiteral("KFind is not installed. Install the 'kfind' package to use advanced search."), QStringLiteral("K Bul kurulu değil. Gelişmiş aramayı kullanmak için 'kfind' paketini kurun.")},
        {QStringLiteral("KFind could not be started."), QStringLiteral("K Bul başlatılamadı.")},
        {QStringLiteral("Built-in recursive search is currently available for local and KIO locations. Use the provider search in a later update."), QStringLiteral("Yerleşik özyineli arama şu anda yerel ve KIO konumlarında kullanılabilir. Bulut sağlayıcı araması daha sonra eklenecek.")},
        {QStringLiteral("Only local executables can be started."), QStringLiteral("Yalnızca yerel çalıştırılabilir dosyalar başlatılabilir.")},
        {QStringLiteral("Executable file does not exist."), QStringLiteral("Çalıştırılabilir dosya mevcut değil.")},
        {QStringLiteral("Could not grant execute permission to the file."), QStringLiteral("Dosyaya çalıştırma izni verilemedi.")},
        {QStringLiteral("Could not start executable."), QStringLiteral("Uygulama başlatılamadı.")},
        {QStringLiteral("Application started."), QStringLiteral("Uygulama başlatıldı.")},
        {QStringLiteral("Ready to move"), QStringLiteral("Taşımaya hazır")},
        {QStringLiteral("Copied to clipboard"), QStringLiteral("Panoya kopyalandı")},
        {QStringLiteral("The clipboard does not contain files."), QStringLiteral("Panoda dosya bulunmuyor.")},
        {QStringLiteral("Moved"), QStringLiteral("Taşındı")},
        {QStringLiteral("Pasted"), QStringLiteral("Yapıştırıldı")},
        {QStringLiteral("New name cannot be empty."), QStringLiteral("Yeni ad boş olamaz.")},
        {QStringLiteral("Renamed"), QStringLiteral("Yeniden adlandırıldı")},
        {QStringLiteral("Renamed."), QStringLiteral("Yeniden adlandırıldı.")},
        {QStringLiteral("Copied"), QStringLiteral("Kopyalandı")},
        {QStringLiteral("Duplicated"), QStringLiteral("Kopyası oluşturuldu")},
        {QStringLiteral("Moved to trash"), QStringLiteral("Çöpe taşındı")},
        {QStringLiteral("Restored from trash"), QStringLiteral("Eski konumuna taşındı")},
        {QStringLiteral("Trash emptied"), QStringLiteral("Çöp boşaltıldı")},
        {QStringLiteral("Deleted"), QStringLiteral("Silindi")},
        {QStringLiteral("Compression currently requires a local file or folder."), QStringLiteral("Sıkıştırma şu anda yerel bir dosya veya klasör gerektiriyor.")},
        {QStringLiteral("Source does not exist."), QStringLiteral("Kaynak mevcut değil.")},
        {QStringLiteral("bsdtar was not found. Install the libarchive package."), QStringLiteral("bsdtar bulunamadı. libarchive paketini kurun.")},
        {QStringLiteral("Compression failed."), QStringLiteral("Sıkıştırma başarısız oldu.")},
        {QStringLiteral("Archive extraction currently requires local files and folders."), QStringLiteral("Arşiv çıkarma şu anda yerel dosya ve klasörler gerektiriyor.")},
        {QStringLiteral("Archive does not exist."), QStringLiteral("Arşiv mevcut değil.")},
        {QStringLiteral("7-Zip was not found."), QStringLiteral("7-Zip bulunamadı.")},
        {QStringLiteral("Could not inspect archive contents."), QStringLiteral("Arşiv içeriği denetlenemedi.")},
        {QStringLiteral("Archive extraction failed."), QStringLiteral("Arşiv çıkarma başarısız oldu.")},
        {QStringLiteral("Invalid Ark drag and drop destination."), QStringLiteral("Ark sürükle-bırak hedefi geçersiz.")},
        {QStringLiteral("Could not connect to Ark's extraction service."), QStringLiteral("Ark çıkarma hizmetine bağlanılamadı.")},
        {QStringLiteral("Ark extraction started."), QStringLiteral("Ark çıkarma işlemi başlatıldı.")},
        {QStringLiteral("Archive extraction completed."), QStringLiteral("Arşiv çıkarma işlemi tamamlandı.")},
        {QStringLiteral("Application could not be found."), QStringLiteral("Uygulama bulunamadı.")},
        {QStringLiteral("Invalid URL"), QStringLiteral("Geçersiz URL")},
        {QStringLiteral("Could not create a secure temporary directory"), QStringLiteral("Güvenli geçici dizin oluşturulamadı")},
        {QStringLiteral("Google Drive is not connected."), QStringLiteral("Google Drive bağlı değil.")},
        {QStringLiteral("Invalid Google Drive destination folder."), QStringLiteral("Google Drive hedef klasörü geçersiz.")},
        {QStringLiteral("No valid local files or folders to upload."), QStringLiteral("Yüklenecek geçerli yerel dosya veya klasör yok.")},
        {QStringLiteral("Another Google Drive upload is already running."), QStringLiteral("Başka bir Google Drive yüklemesi zaten çalışıyor.")},
        {QStringLiteral("Upload completed."), QStringLiteral("Yükleme tamamlandı.")},
        {QStringLiteral("Invalid Google Drive move source or destination."), QStringLiteral("Google Drive taşıma kaynağı veya hedefi geçersiz.")},
        {QStringLiteral("Item is already in that Google Drive folder."), QStringLiteral("Öğe zaten bu Google Drive klasöründe.")},
        {QStringLiteral("Moved in Google Drive."), QStringLiteral("Google Drive içinde taşındı.")},
        {QStringLiteral("Invalid Google Drive copy source or destination."), QStringLiteral("Google Drive kopyalama kaynağı veya hedefi geçersiz.")},
        {QStringLiteral("Another Google Drive copy is already running."), QStringLiteral("Başka bir Google Drive kopyalama işlemi zaten çalışıyor.")},
        {QStringLiteral("Copied in Google Drive."), QStringLiteral("Google Drive içinde kopyalandı.")},
        {QStringLiteral("Google Drive did not return the created folder id."), QStringLiteral("Google Drive oluşturulan klasör kimliğini döndürmedi.")},
        {QStringLiteral("Google Drive did not return the copied folder id."), QStringLiteral("Google Drive kopyalanan klasör kimliğini döndürmedi.")},
        {QStringLiteral("Invalid Google Drive file."), QStringLiteral("Google Drive dosyası geçersiz.")},
        {QStringLiteral("Opened the existing synced local copy."), QStringLiteral("Mevcut eşitlenmiş yerel kopya açıldı.")},
        {QStringLiteral("Downloaded. Changes will be synced back to Google Drive."), QStringLiteral("İndirildi. Değişiklikler Google Drive'a geri eşitlenecek.")},
        {QStringLiteral("Invalid Google Drive file or download folder."), QStringLiteral("Google Drive dosyası veya indirme klasörü geçersiz.")},
        {QStringLiteral("Google-native documents are not downloaded as binary files yet."), QStringLiteral("Google'a özgü belgeler henüz ikili dosya olarak indirilmiyor.")},
        {QStringLiteral("Changes synced to Google Drive."), QStringLiteral("Değişiklikler Google Drive'a eşitlendi.")},
        {QStringLiteral("Google Drive is not connected. Return to Home and connect your account."), QStringLiteral("Google Drive bağlı değil. Ana Sayfa'ya dönüp hesabınızı bağlayın.")},
        {QStringLiteral("Folder does not exist."), QStringLiteral("Klasör mevcut değil.")},
        {QStringLiteral("Invalid Google Drive location."), QStringLiteral("Google Drive konumu geçersiz.")},
        {QStringLiteral("Google Drive session expired or is invalid. Reconnect Google Drive from Home."), QStringLiteral("Google Drive oturumunun süresi dolmuş veya oturum geçersiz. Ana Sayfa'dan yeniden bağlanın.")},
        {QStringLiteral("Invalid Google Drive item or name."), QStringLiteral("Google Drive öğesi veya adı geçersiz.")},
        {QStringLiteral("Invalid Google Drive item."), QStringLiteral("Google Drive öğesi geçersiz.")},
        {QStringLiteral("Moved to Google Drive trash."), QStringLiteral("Google Drive çöpüne taşındı.")},
        {QStringLiteral("Permanently deleted from Google Drive."), QStringLiteral("Google Drive'dan kalıcı olarak silindi.")},
        {QStringLiteral("Open a Google Drive folder before uploading."), QStringLiteral("Yüklemeden önce bir Google Drive klasörü açın.")},
        {QStringLiteral("Upload source is not a file."), QStringLiteral("Yükleme kaynağı bir dosya değil.")},
        {QStringLiteral("Uploaded to Google Drive."), QStringLiteral("Google Drive'a yüklendi.")}
    };

    const auto it = exact.constFind(message);
    if (it != exact.constEnd())
        return it.value();

    const QList<QPair<QString, QString>> prefixes = {
        {QStringLiteral("Archive created: "), QStringLiteral("Arşiv oluşturuldu: ")},
        {QStringLiteral("Could not create extraction folder: "), QStringLiteral("Çıkarma klasörü oluşturulamadı: ")},
        {QStringLiteral("Extracted to: "), QStringLiteral("Şuraya çıkarıldı: ")},
        {QStringLiteral("Downloaded to "), QStringLiteral("Şuraya indirildi: ")}
    };
    for (const auto &prefix : prefixes) {
        if (message.startsWith(prefix.first))
            return prefix.second + message.mid(prefix.first.size());
    }

    return message;
}

void LanguageManager::buildDictionary()
{
    auto &en = m_dictionary[QStringLiteral("en")];
    auto &tr = m_dictionary[QStringLiteral("tr")];

    const QList<QPair<QString, QPair<QString, QString>>> rows = {
        {"home", {"Home", "Ana Sayfa"}}, {"dashboard", {"Explore", "Keşfet"}}, {"home_folder", {"Home", "Ev Dizini"}}, {"favorites", {"Favorites", "Favoriler"}},
        {"desktop", {"Desktop", "Masaüstü"}}, {"downloads", {"Downloads", "İndirilenler"}},
        {"documents", {"Documents", "Belgeler"}}, {"pictures", {"Pictures", "Resimler"}},
        {"music", {"Music", "Müzik"}}, {"videos", {"Videos", "Videolar"}},
        {"trash", {"Trash", "Çöp"}}, {"root", {"Administrator", "Yönetici"}},
        {"network", {"Network", "Ağ"}}, {"settings", {"Settings", "Ayarlar"}},
        {"storage_cloud", {"Storage & Cloud", "Depolama ve Bulut"}}, {"cloud_integrations", {"Cloud Integrations", "Bulut Entegrasyonları"}}, {"disks", {"Disks", "Diskler"}}, {"refresh", {"Refresh", "Yenile"}},
        {"quick_access", {"Quick Access", "Hızlı Erişim"}}, {"pinned_folders", {"Pinned Folders", "Sabitlenmiş Klasörler"}},
        {"storage_overview", {"Storage overview", "Depolama özeti"}}, {"create_new", {"Create new", "Yeni oluştur"}},
        {"new_folder", {"New folder", "Yeni klasör"}}, {"grid", {"Grid", "Izgara"}}, {"list", {"List", "Liste"}},
        {"open", {"Open", "Aç"}}, {"rename", {"Rename", "Yeniden adlandır"}},
        {"select_all", {"Select all", "Tümünü seç"}}, {"clear_selection", {"Clear selection", "Seçimi temizle"}},
        {"duplicate", {"Duplicate", "Kopyasını oluştur"}}, {"copy_to", {"Copy to…", "Şuraya kopyala…"}},
        {"move_to", {"Move to…", "Şuraya taşı…"}}, {"move_trash", {"Move to Trash", "Çöpe taşı"}},
        {"delete_permanently", {"Delete permanently", "Kalıcı olarak sil"}},
        {"add_favorite", {"Add to Favorites", "Favorilere ekle"}}, {"remove_favorite", {"Remove from Favorites", "Favorilerden çıkar"}},
        {"split_view", {"Split view", "Bölünmüş görünüm"}}, {"close_split", {"Close split", "Bölünmüş görünümü kapat"}},
        {"new_tab", {"New tab", "Yeni sekme"}}, {"wheel_speed", {"Wheel scroll speed", "Tekerlek kaydırma hızı"}}, {"folder", {"Folder", "Klasör"}},
        {"items", {"items", "öğe"}}, {"language", {"Language", "Dil"}},
        {"connect", {"Connect account", "Hesabı bağla"}}, {"connected", {"Connected", "Bağlı"}},
        {"mounted", {"Mounted", "Bağlı"}}, {"not_mounted", {"Not mounted", "Bağlı değil"}},
        {"mount", {"Mount", "Bağla"}}, {"unmount", {"Unmount", "Bağlantıyı kes"}},
        {"mounting", {"Mounting…", "Bağlanıyor…"}}, {"unmounting", {"Unmounting…", "Bağlantı kesiliyor…"}},
        {"disk_mounted", {"Disk mounted", "Disk bağlandı"}}, {"disk_unmounted", {"Disk unmounted", "Disk bağlantısı kesildi"}},
        {"mount_failed", {"Could not mount disk", "Disk bağlanamadı"}}, {"unmount_failed", {"Could not unmount disk", "Disk bağlantısı kesilemedi"}},
        {"oauth_pending", {"OAuth pending", "OAuth bekliyor"}}, {"smb", {"SMB share", "SMB paylaşımı"}},
        {"sftp", {"SFTP server", "SFTP sunucusu"}}, {"cancel", {"Cancel", "İptal"}},
        {"save", {"Save", "Kaydet"}}, {"create", {"Create", "Oluştur"}},
        {"undo", {"Undo", "Geri al"}}, {"redo", {"Redo", "Yinele"}},
        {"cut", {"Cut", "Kes"}}, {"paste", {"Paste", "Yapıştır"}},
        {"copy", {"Copy", "Kopyala"}}, {"move", {"Move", "Taşı"}},
        {"delete", {"Delete", "Sil"}}, {"dont_ask_again", {"Don't ask again", "Bir daha sorma"}}, {"connect_cloud", {"Connect cloud storage", "Bulut depolamayı bağla"}},
        {"google_drive", {"Google Drive", "Google Drive"}}, {"onedrive", {"OneDrive", "OneDrive"}},
        {"dropbox", {"Dropbox", "Dropbox"}}, {"not_configured", {"Client ID required", "Client ID gerekli"}},
        {"search", {"Search files, folders and connected storage…", "Dosya, klasör ve bağlı depolamalarda ara…"}},
        {"search_here", {"Search from here", "Buradan ara"}}, {"search_everywhere", {"Search everywhere", "Her yerde ara"}},
        {"searching", {"Searching…", "Aranıyor…"}}, {"results", {"results", "sonuç"}}, {"open_kfind", {"Open KFind", "K Bul'u Aç"}},
        {"open_as_administrator", {"Open as Administrator", "Yönetici olarak aç"}},
        {"admin_file_opened", {"Administrator file opened in a temporary editor session", "Yönetici dosyası geçici düzenleme oturumunda açıldı"}},
        {"admin_file_saved", {"Administrator file synchronized", "Yönetici dosyası eşitlendi"}},
        {"view", {"View", "Görünüm"}}, {"sort_by", {"Sort by", "Sırala"}},
        {"sort_name", {"Name", "Ad"}}, {"sort_date", {"Modified date", "Değiştirilme tarihi"}}, {"sort_created", {"Date", "Tarih"}},
        {"sort_size", {"Size", "Boyut"}}, {"sort_type", {"Type", "Tür"}},
        {"ascending", {"Ascending", "Artan"}}, {"descending", {"Descending", "Azalan"}},
        {"show_hidden", {"Show hidden files", "Gizli dosyaları göster"}},
        {"hidden_top", {"Hidden items at top", "Gizli öğeler en üstte"}},
        {"hidden_normal", {"Hidden items in normal order", "Gizli öğeler normal sırada"}},
        {"hidden_bottom", {"Hidden items at bottom", "Gizli öğeler en altta"}},
        {"default_icons", {"Use default icons", "Varsayılan ikonları kullan"}},
        {"system_icons", {"Use system icons", "Sistem ikonlarını kullan"}},
        {"kio_progress", {"KIO operation notifications", "KIO işlem bildirimleri"}},
        {"directory_menu", {"Folder menu", "Dizin menüsü"}},
        {"compress", {"Compress", "Sıkıştır"}}, {"open_with", {"Open With", "Birlikte Aç"}},
        {"properties", {"Properties", "Özellikler"}}, {"default_app", {"Default", "Varsayılan"}},
        {"no_compatible_apps", {"No compatible applications found.", "Uyumlu uygulama bulunamadı."}},
        {"always_open_with", {"Always open this file type with", "Bu dosya türünü her zaman şununla aç"}},
        {"set_default_app", {"Set as default application", "Varsayılan uygulama yap"}},
        {"default_app_changed", {"Default application changed", "Varsayılan uygulama değiştirildi"}},
        {"location", {"Location", "Konum"}}, {"size", {"Size", "Boyut"}},
        {"modified", {"Modified", "Değiştirilme"}}, {"mime_type", {"MIME type", "MIME türü"}},
        {"application_actions", {"Application Actions", "Uygulama Eylemleri"}}, {"pin_action", {"Pin action", "Eylemi sabitle"}}, {"unpin_action", {"Unpin action", "Eylemi kaldır"}}, {"close", {"Close", "Kapat"}}
    };

    for (const auto &row : rows) {
        en.insert(row.first, row.second.first);
        tr.insert(row.first, row.second.second);
    }
}
