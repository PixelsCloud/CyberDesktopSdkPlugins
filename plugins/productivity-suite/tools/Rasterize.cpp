#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 3)
        return 2;
    QDir source(QString::fromLocal8Bit(argv[1])), target(QString::fromLocal8Bit(argv[2]));
    QDir().mkpath(target.path());
    const QMap<QString, QString> names{{"timer", "timer"},     {"todo", "list-todo"},  {"countdown", "calendar-clock"},
                                       {"network", "network"}, {"disk", "hard-drive"}, {"notes", "sticky-note"},
                                       {"rss", "rss"}};
    for (auto it = names.begin(); it != names.end(); ++it) {
        QFile file(source.filePath(it.value() + ".svg"));
        if (!file.open(QIODevice::ReadOnly))
            return 3;
        QSvgRenderer svg(file.readAll().replace("currentColor", "#69b8cf"));
        if (!svg.isValid())
            return 4;
        QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            svg.render(&painter, QRectF(12, 12, 232, 232));
        }
        if (!image.save(target.filePath(it.key() + ".png")))
            return 5;
    }
    return 0;
}
