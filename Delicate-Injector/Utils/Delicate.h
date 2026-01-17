#pragma once

#include "imgui.h"

namespace Delicate {
    class UISettings {
      public:
        class Colors {
          public:
            static inline ImColor bgColor            = ImColor(14, 12, 18, 255);
            static inline ImColor headerColor        = ImColor(75, 0, 130, 255);
            static inline ImColor panelColor         = ImColor(18, 16, 24, 255);
            static inline ImColor rowColor           = ImColor(25, 25, 33, 255);
            static inline ImColor rowHover           = ImColor(35, 35, 46, 255);
            static inline ImColor textColor          = ImColor(255, 255, 255, 255);
            static inline ImColor accentColor        = ImColor(27, 13, 54, 255);
            static inline ImColor borderColor        = ImColor(255, 255, 255, 32);
            static inline ImColor tabsColor          = ImColor(22, 20, 28, 255);
            static inline ImColor sliderBg           = ImColor(40, 40, 52, 255);
            static inline ImColor buttonColor        = ImColor(50, 50, 62, 255);
            static inline ImColor buttonHoverColor   = ImColor(60, 60, 72, 255);
            static inline ImColor mutedTextColor     = ImColor(170, 170, 170, 255);
            static inline ImColor checkboxBgColor    = ImColor(15, 15, 15, 255);
            static inline ImColor tooltipBgColor     = ImColor(20, 20, 20, 255);
            static inline ImColor tooltipTextColor   = ImColor(255, 255, 255, 255);
            static inline ImColor tooltipShadowColor = ImColor(0, 0, 0, 255);
        };
    };
} // namespace Delicate
