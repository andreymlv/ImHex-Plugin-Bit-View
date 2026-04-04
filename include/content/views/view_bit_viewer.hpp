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

        // Describes the visible portion of the grid after layout and scroll calculations
        struct GridLayout {
            u64 baseAddress;
            u64 totalBytes;
            u64 totalBits;
            i64 bitsPerRow;

            float cellSize;
            float cellStride;   // cellSize + gap

            u64 totalRows;
            u64 actualVisibleRows;  // rows that fit in the visible window area
            u64 firstRow;
            u64 lastRow;        // exclusive
            i64 firstVisCol;
            i64 lastVisCol;     // exclusive

            // Origin for drawing, adjusted to avoid float precision issues
            float drawOriginX;
            float drawOriginY;
        };

        // Information about which cell the mouse is hovering over
        struct HitTestResult {
            bool valid = false;
            u64 row = 0;
            i64 col = -1;
            u64 byteIdx = 0;
        };

        GridLayout computeLayout(prv::Provider *provider);
        void handleScrollToSelection(GridLayout &layout);
        void readVisibleBytes(prv::Provider *provider, const GridLayout &layout);
        u8 readByte(const GridLayout &layout, u64 row, u64 globalByteIdx) const;
        HitTestResult hitTest(const GridLayout &layout) const;
        void handleMouseInput(const GridLayout &layout, const HitTestResult &hit);
        void handleKeyboardInput(GridLayout &layout, float scrollMaxY, u64 maxFirstRow);
        void drawGrid(const GridLayout &layout);
        void drawHoverTooltip(const GridLayout &layout, const HitTestResult &hit);
        void syncSelectionToHexEditor(u64 baseAddress);

        // Grid appearance
        int m_pixelSize = 1;
        BitOrder m_bitOrder = BitOrder::MsbFirst;
        ImVec4 m_colorZero = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        ImVec4 m_colorOne  = ImVec4(0.0f, 0.8f, 0.4f, 1.0f);

        // Row width
        bool m_fitToWidth = false;
        i64 m_bitsPerRow = 8192;
        std::string m_bitsPerRowInput = "8192";
        wolv::math_eval::MathEvaluator<i64> m_evaluator;
        std::optional<i64> m_lastEvalResult = 8192;

        // Selection and cursor
        std::optional<Region> m_selectedRegion;
        u64 m_cursorOffset = 0;
        u64 m_selectionAnchor = 0;
        bool m_hasCursor = false;

        // Scroll state
        bool m_shouldScrollToSelection = false;
        bool m_needsSyncToHexEditor = false;
        bool m_suppressNextExternalEvent = false;
        std::optional<u64> m_targetFirstRow;
        std::optional<i64> m_targetFirstVisCol;

        // Compact-read bookkeeping (set by readVisibleBytes, used by readByte)
        std::vector<u8> m_readBuffer;
        bool m_useCompactRead = false;
        u64 m_compactBytesPerRow = 0;
        u64 m_compactFirstVisCol = 0;
        u64 m_fullFirstByte = 0;
    };

}
