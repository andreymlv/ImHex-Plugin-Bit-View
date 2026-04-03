#include <hex/plugin.hpp>

#include <hex/api/content_registry/views.hpp>
#include <hex/api/localization_manager.hpp>

#include <romfs/romfs.hpp>

#include "content/views/view_bit_viewer.hpp"

using namespace hex;
using namespace hex::plugin::bitview;

IMHEX_PLUGIN_SETUP("Bit View", "Andrey Malov", "Visualizes binary data as a grid of colored bits") {

    hex::LocalizationManager::addLanguages(
        romfs::get("lang/languages.json").string(),
        [](const std::filesystem::path &path) {
            return romfs::get(path).string();
        }
    );

    ContentRegistry::Views::add<ViewBitViewer>();

}
