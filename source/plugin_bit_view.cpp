#include <hex/plugin.hpp>

#include <hex/api/content_registry/views.hpp>

#include "content/views/view_bit_viewer.hpp"

using namespace hex;
using namespace hex::plugin::bitview;

IMHEX_PLUGIN_SETUP("Bit View", "Andrey Malov", "Visualizes binary data as a grid of colored bits") {

    ContentRegistry::Views::add<ViewBitViewer>();

}
