#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 3)
        return 2;
    QDir source(QString::fromLocal8Bit(argv[1])), target(QString::fromLocal8Bit(argv[2]));
    QDir().mkpath(target.path());
    const QMap<QString, QByteArray> colors{{"leaf", "#74e0b3"},         {"wind", "#f0ce73"},
                                           {"cloud-fog", "#edaf70"},    {"factory", "#ef7f91"},
                                           {"shield-alert", "#b99cee"}, {"triangle-alert", "#e199b5"},
                                           {"map-pin", "#a9c6d3"},      {"circle-help", "#94a5b2"}};
    int count = 0;
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        QFile input(source.filePath(it.key() + ".svg"));
        if (!input.open(QIODevice::ReadOnly))
            return 3;
        auto svg = input.readAll().replace("currentColor", it.value());
        QSvgRenderer renderer(svg);
        if (!renderer.isValid())
            return 4;
        QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            renderer.render(&painter, QRectF(12, 12, 232, 232));
        }
        if (!image.save(target.filePath(it.key() + ".png")))
            return 5;
        ++count;
    }
    std::printf("%d Lucide icons rasterized\n", count);
    return count == 8 ? 0 : 6;
}
