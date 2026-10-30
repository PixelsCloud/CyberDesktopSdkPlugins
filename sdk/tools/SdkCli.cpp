#include <sdk/Version.h>

#include <iostream>

#include "tools/Diagnostics.h"
#include "tools/PreviewImages.h"
#include "tools/PreviewOptions.h"
#include "tools/PreviewState.h"
#include "tools/Runtime.h"

int wmain(int argc, wchar_t** wide) {
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i)
        arguments.push_back(cdsdk::utf8Argument(wide[i]));
    std::vector<const char*> argv;
    for (const auto& argument : arguments)
        argv.push_back(argument.c_str());
    try {
        if (argc == 2 && arguments[1] == "--version") {
            std::cout << cdsdk::version << '\n';
            return 0;
        }
        if (argc < 3) {
            std::cout << "CyberDesktop SDK " << cdsdk::version
                      << " (C ABI 1, protobuf package 1)\n"
                         "cd-sdk inspect PACKAGE|DIRECTORY\ncd-sdk validate PACKAGE\ncd-sdk "
                         "descriptor TEXT_PROTO OUTPUT_PB\n"
                         "cd-sdk pack DIRECTORY OUTPUT_CDPKG\ncd-sdk extract PACKAGE "
                         "EMPTY_DIRECTORY\ncd-sdk test DIRECTORY\ncd-sdk diagnostics DIRECTORY OUTPUT_JSON\n"
                         "cd-sdk compare EXPECTED ACTUAL DIFF.png [TOLERANCE [ALLOWED_RATIO]]\ncd-sdk state FILE.pb\n";
            return argc == 2 && arguments[1] == "--help" ? 0 : 2;
        }
        const std::string command = argv[1];
        const auto path = cdsdk::utf8Path(argv[2]);
        cdsdk::Error error;
        bool ok = false;
        if (command == "state" && argc == 3) {
            const auto state = cdsdk::readPreviewState(path);
            std::string json;
            const auto status = google::protobuf::util::MessageToJsonString(state, &json);
            if (!status.ok()) {
                throw std::runtime_error(status.ToString());
            }
            std::cout << json << '\n';
            return 0;
        } else if (command == "compare" && (argc == 5 || argc == 6 || argc == 7)) {
            cdsdk::PreviewImages images;
            const auto tolerance = argc >= 6 ? cdsdk::PreviewOptions::integer(wide[5], 255) : 0;
            size_t used = 0;
            const double allowed = argc == 7 ? std::stod(argv[6], &used) : 0;
            if (argc == 7 && used != arguments[6].size()) {
                throw std::invalid_argument("Invalid allowed ratio");
            }
            if (tolerance > 255 || !std::isfinite(allowed) || allowed < 0 || allowed > 1) {
                throw std::invalid_argument("Tolerance must be 0..255 and allowed ratio 0..1");
            }
            const auto difference =
                cdsdk::compareImages(images.load(path), images.load(cdsdk::utf8Path(argv[3])), uint32_t(tolerance));
            images.save(cdsdk::utf8Path(argv[4]), difference.highlight.width, difference.highlight.height,
                        difference.highlight.rgba);
            std::cout << "pixels=" << difference.pixels << " differing=" << difference.differing
                      << " ratio=" << difference.ratio << " maxDelta=" << difference.maximumDelta
                      << " mae=" << difference.meanAbsoluteError << '\n';
            return difference.ratio <= allowed ? 0 : 1;
        } else if (command == "diagnostics" && argc == 4) {
            ok = cdsdk::diagnosticDirectory(path, cdsdk::utf8Path(argv[3]), &error);
        } else if (command == "descriptor" && argc == 4) {
            cdsdk::pb::PluginDescriptor descriptor;
            ok = cdsdk::readDescriptor(path, &descriptor, &error) &&
                 cdsdk::writeDescriptor(cdsdk::utf8Path(argv[3]), descriptor, &error);
        } else if (command == "pack" && argc == 4)
            ok = cdsdk::packDirectory(path, cdsdk::utf8Path(argv[3]), &error);
        else if (command == "inspect" && argc == 3 && std::filesystem::is_directory(path)) {
            cdsdk::pb::PluginDescriptor descriptor;
            ok = cdsdk::loadDirectoryDescriptor(path, &descriptor, &error);
            if (ok) {
                std::cout << cdsdk::descriptorJson(descriptor) << '\n';
            }
        } else if ((command == "inspect" || command == "validate") && argc == 3) {
            cdsdk::pb::PluginPackage package;
            ok = cdsdk::readPackage(path, &package, &error);
            if (ok && command == "inspect")
                std::cout << cdsdk::descriptorJson(package.plugin()) << '\n';
        } else if (command == "extract" && argc == 4) {
            cdsdk::pb::PluginPackage package;
            ok = cdsdk::readPackage(path, &package, &error) &&
                 cdsdk::extractPackage(package, cdsdk::utf8Path(argv[3]), &error);
        } else if (command == "test" && argc == 3)
            ok = cdsdk::lifecycle(path, &error);
        else {
            std::cerr << "cli.arguments: Invalid command or argument count\n";
            return 2;
        }
        if (!ok) {
            std::cerr << cdsdk::errorText(error) << '\n';
            return 1;
        }
        if (command != "inspect")
            std::cout << "PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk.exception: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "sdk.exception: Unknown failure\n";
        return 1;
    }
}
