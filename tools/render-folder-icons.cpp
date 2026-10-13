#include <QCoreApplication>
#include <QDir>
#include <QImageReader>
#include <cstdio>

// Bake original-color folder art; SVGs remain the editable source/resources.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 3 || !QDir().mkpath(QString::fromLocal8Bit(argv[2]))) return 1;
    for (int extent = 64; extent <= 512; extent += 64) {
        QImageReader reader(QString::fromLocal8Bit(argv[1]));
        const QSize original = reader.size();
        reader.setScaledSize(original.scaled(QSize(extent, extent), Qt::KeepAspectRatio));
        const QImage image = reader.read();
        const QString output = QString::fromLocal8Bit(argv[2]) + QLatin1Char('/')
            + QString::number(extent) + QStringLiteral(".png");
        if (!original.isValid() || image.isNull() || !image.save(output, "PNG")) {
            std::fprintf(stderr, "Cannot render folder artwork: %s\n", argv[1]);
            return 1;
        }
    }
    return 0;
}
