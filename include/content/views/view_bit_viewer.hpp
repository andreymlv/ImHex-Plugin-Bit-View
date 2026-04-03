#pragma once

#include <hex/ui/view.hpp>
#include <hex/api/imhex_api/hex_editor.hpp>

#include <wolv/math_eval/math_evaluator.hpp>

#include <string>
#include <vector>

namespace hex::plugin::bitview {

    class ViewBitViewer : public View::Window {
    public:
        explicit ViewBitViewer();
        ~ViewBitViewer() override;

        void drawContent() override;
        void drawHelpText() override;

    private:
        void drawSettings();
        void drawBitGrid(prv::Provider *provider);

        enum class BitOrder : int { MsbFirst = 0, LsbFirst = 1 };

        bool m_fitToWidth = true;
        i64 m_bitsPerRow = 64;
        std::string m_bitsPerRowInput = "64";
        int m_pixelSize = 6;
        int m_cellGap = 0;
        BitOrder m_bitOrder = BitOrder::MsbFirst;
        ImVec4 m_colorZero = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        ImVec4 m_colorOne  = ImVec4(0.0f, 0.8f, 0.4f, 1.0f);

        wolv::math_eval::MathEvaluator<i64> m_evaluator;
        std::optional<i64> m_lastEvalResult = 64;

        std::optional<Region> m_selectedRegion;
        bool m_shouldScrollToSelection = false;

        std::vector<u8> m_readBuffer;
    };

}
