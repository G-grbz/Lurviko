#include "sftpdirectory.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class SftpTests : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString write(const QString& name, const QByteArray& data) {
        const QString path = root.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path); if (file.open(QIODevice::WriteOnly)) file.write(data);
        return path;
    }
private slots:
    void parsesConfigWithIncludesAndPatterns() {
        write("ssh/config", R"(
# comment
Host *
    ServerAliveInterval 30
Host nas backup !evil
    HostName 192.168.1.10
    User admin
    Port 2222
    User ignored-second-value
Host web.example.com
Match host foo
    User matched
Host --oops bad;name
Include conf.d/*.conf
)");
        write("ssh/conf.d/work.conf", "Host work=\nHost work\n  HostName work.internal\n  Port=2200\n");
        const auto hosts = SftpDirectory::parseConfig(root.filePath("ssh/config"));
        QStringList aliases;
        for (const auto& value : hosts) aliases << value.toMap().value("alias").toString();
        QCOMPARE(aliases, QStringList({"nas", "backup", "web.example.com", "work"}));
        const auto nas = hosts.at(0).toMap();
        QCOMPARE(nas.value("hostName").toString(), QString("192.168.1.10"));
        QCOMPARE(nas.value("user").toString(), QString("admin"));
        QCOMPARE(nas.value("port").toInt(), 2222);
        QCOMPARE(hosts.at(2).toMap().value("user").toString(), QString());   // Match block ignored
        QCOMPARE(hosts.at(3).toMap().value("port").toInt(), 2200);
    }
    void parsesKnownHosts() {
        const auto path = write("ssh/known_hosts",
            "github.com,140.82.121.4 ssh-ed25519 AAAA\n"
            "|1|hash=|salt= ssh-ed25519 AAAA\n"
            "[nas.local]:2222 ssh-ed25519 AAAA\n"
            "@revoked bad.example ssh-rsa AAAA\n"
            "@cert-authority *.corp ssh-rsa AAAA\n"
            "github.com ssh-rsa AAAA\n"
            "# comment\n");
        const auto hosts = SftpDirectory::parseKnownHosts(path);
        QStringList found;
        for (const auto& value : hosts)
            found << value.toMap().value("host").toString() + ':' + value.toMap().value("port").toString();
        QCOMPARE(found, QStringList({"github.com:22", "140.82.121.4:22", "nas.local:2222"}));
    }
    void parsesTypedTargets_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QString>("url");
        QTest::newRow("host") << "server" << true << "sftp://server";
        QTest::newRow("user-host-port") << "me@server:2222" << true << "sftp://me@server:2222";
        QTest::newRow("path") << "me@server:2222/srv/data" << true << "sftp://me@server:2222/srv/data";
        QTest::newRow("scp") << "server:/var/www" << true << "sftp://server/var/www";
        QTest::newRow("scp-relative") << "server:docs" << true << "sftp://server";
        QTest::newRow("tilde") << "sftp://server/~/x" << true << "sftp://server/~/x";
        QTest::newRow("url") << "sftp://me@10.0.0.2:22/home/me" << true << "sftp://me@10.0.0.2/home/me";
        QTest::newRow("ipv6") << "[fe80::1]:2200/tmp" << true << "sftp://[fe80::1]:2200/tmp";
        QTest::newRow("domain-user") << "first.last@corp.example" << true << "sftp://first.last@corp.example";
        QTest::newRow("option") << "-oProxyCommand=evil" << false << "";
        QTest::newRow("bad-port") << "server:70000" << false << "";
        QTest::newRow("spaces") << "my server" << false << "";
        QTest::newRow("http") << "http://server" << false << "";
        QTest::newRow("empty") << "  " << false << "";
    }
    void parsesTypedTargets() {
        QFETCH(QString, text); QFETCH(bool, valid); QFETCH(QString, url);
        SftpDirectory directory;
        const auto parsed = directory.parseTarget(text);
        QCOMPARE(parsed.value("valid").toBool(), valid);
        if (!valid) { QVERIFY(!text.trimmed().isEmpty() ? !parsed.value("error").toString().isEmpty() || !valid : true); return; }
        QCOMPARE(directory.buildUrl(parsed.value("host").toString(), parsed.value("user").toString(),
                                    parsed.value("port").toInt(), parsed.value("path").toString()), url);
    }
    void buildUrlRejectsInjection() {
        SftpDirectory directory;
        QVERIFY(directory.buildUrl("-oProxyCommand=x", "", 22, "").isEmpty());
        QVERIFY(directory.buildUrl("host", "bad user", 22, "").isEmpty());
        QVERIFY(directory.buildUrl("host", "", 0, "").startsWith("sftp://host"));
        QVERIFY(directory.buildUrl("host", "", 22, "a\nb").isEmpty());
        QCOMPARE(directory.buildUrl("host", "", 22, "/a b/../c"), QString("sftp://host/c"));
        QCOMPARE(directory.buildUrl("host", "", 22, "/Belgeler ğ"), QString("sftp://host/Belgeler%20%C4%9F"));
    }
    void refreshMergesSources() {
        write("home/.ssh/config", "Host nas\n  HostName 192.168.1.10\nHost github.com\n  User git\nHost mygit\n  HostName git.internal\n  User git\n");
        write("home/.ssh/known_hosts", "192.168.1.10 ssh-ed25519 AAAA\nother.example ssh-ed25519 AAAA\n"
              "github.com ssh-ed25519 AAAA\naur.archlinux.org ssh-ed25519 AAAA\ngit.internal ssh-ed25519 AAAA\n");
        SftpDirectory directory;
        directory.setSshDirectoryForTest(root.filePath("home/.ssh"));
        // printf '%s': dash's echo (Debian's /bin/sh) would turn "\032" into
        // a control character before the parser sees avahi's escape.
        directory.setAvahiProgramForTest(write("fake-avahi", QByteArray("#!/bin/sh\n") +
            "printf '%s\\n' '+;eth0;IPv4;nas;_ssh._tcp;local'\n"
            "printf '%s\\n' '=;eth0;IPv4;My\\032NAS\\0462;_ssh._tcp;local;nas.local;192.168.1.10;22;'\n"
            "printf '%s\\n' '=;eth0;IPv4;Bad;_ssh._tcp;local;-oX;1.2.3.4;22;'\n"));
        QFile::setPermissions(root.filePath("fake-avahi"), QFile::ReadOwner | QFile::ExeOwner);
        QSignalSpy hosts(&directory, &SftpDirectory::hostsChanged);
        directory.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!directory.loading() && !directory.discovering(), 10000);
        QCOMPARE(directory.configHosts().size(), 1);
        QCOMPARE(directory.configHosts().at(0).toMap().value("url").toString(), QString("sftp://nas"));
        // Hosts in the config are not repeated as known hosts.
        QCOMPARE(directory.knownHosts().size(), 1);
        QCOMPARE(directory.knownHosts().at(0).toMap().value("host").toString(), QString("other.example"));
        QCOMPARE(directory.discovered().size(), 1);
        const auto found = directory.discovered().at(0).toMap();
        QCOMPARE(found.value("name").toString(), QString("My NAS.2"));
        QCOMPARE(found.value("url").toString(), QString("sftp://nas.local"));
        QVERIFY(directory.discoveryError().isEmpty());
    }
    void discoveryWithoutAvahi() {
        SftpDirectory directory;
        directory.setAvahiProgramForTest(QString());
        directory.discover();
        QVERIFY(!directory.discovering());
        QVERIFY(!directory.discoveryError().isEmpty());
    }
};
QTEST_GUILESS_MAIN(SftpTests)
#include "sftp_tests.moc"
