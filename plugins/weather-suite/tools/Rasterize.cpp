#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 3)
        return 2;
    QDir source(QString::fromLocal8Bit(argv[1]));
    QDir target(QString::fromLocal8Bit(argv[2]));
    QDir().mkpath(target.path());
    int count = 0;
    for (const auto& name : source.entryList({"*.svg"}, QDir::Files)) {
        QSvgRenderer renderer(source.filePath(name));
        if (!renderer.isValid())
            return 3;
        QImage image(512, 512, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            renderer.render(&painter);
        }
        QRect bounds;
        for (int y = 0; y < 512; ++y)
            for (int x = 0; x < 512; ++x)
                if (qAlpha(image.pixel(x, y)))
                    bounds = bounds.united(QRect(x, y, 1, 1));
        if (bounds.isEmpty())
            return 4;
        const auto cropped = image.copy(bounds.adjusted(-3, -3, 3, 3).intersected(image.rect()));
        if (!cropped.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                 .save(target.filePath(name.left(name.size() - 4) + ".png")))
            return 5;
        ++count;
    }
    std::printf("%d static Meteocons icons rasterized\n", count);
    return count == 18 ? 0 : 6;
}
