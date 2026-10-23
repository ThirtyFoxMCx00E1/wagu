#include "vkeyboard.h"
#include "ui_common.h"

#include <string>
#include <string.h>

namespace {
bool s_isOpen = false;
bool s_shiftEnabled = false;
bool s_symbolsEnabled = false;
bool s_numericLayout = false;
int s_maxTextLength = 128;
std::string s_text;
std::string s_title;
VKeyboard::SubmitFn s_submitCallback = nullptr;
void* s_callbackData = nullptr;

const char* kLowercaseRows[] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
};

const char* kUppercaseRows[] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM",
};

const char* kSymbolRows[] = {
    "1234567890",
    "@#$%&*-+()",
    "!?\"':;/=_",
    ".,<>[]~",
};

const char* kNumericRows[] = {
    "123",
    "456",
    "789",
    ".0",
};
}

void VKeyboard::Open(const char* title, const char* initial, int maxLen,
                     SubmitFn submitCallback, void* callbackData, bool numeric) {
    s_isOpen = true;
    s_title = title ? title : "";
    s_text = initial ? initial : "";
    s_maxTextLength = maxLen > 0 ? maxLen : 128;
    s_submitCallback = submitCallback;
    s_callbackData = callbackData;
    s_numericLayout = numeric;
    s_shiftEnabled = false;
    s_symbolsEnabled = false;
}

void VKeyboard::Close() {
    s_isOpen = false;
}

bool VKeyboard::IsOpen() {
    return s_isOpen;
}

static bool DrawKeyboardKey(const char* label, float width, float height,
                            ImU32 color = 0) {
    if (color) {
        ImGui::PushStyleColor(
            ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(color));
    }

    const bool wasClicked = ImGui::Button(label, ImVec2(width, height));

    if (color) {
        ImGui::PopStyleColor();
    }

    return wasClicked;
}

static void AppendCharacter(char character) {
    if (static_cast<int>(s_text.size()) < s_maxTextLength - 1) {
        s_text.push_back(character);
    }
}

void VKeyboard::Render() {
    if (!s_isOpen) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float padding = UI::S(10.0f);
    const float keyboardHeight = displaySize.y * 0.50f;


    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), displaySize, IM_COL32(0, 0, 0, 90));

    ImGui::SetNextWindowPos(
        ImVec2(0, displaySize.y - keyboardHeight));
    ImGui::SetNextWindowSize(ImVec2(displaySize.x, keyboardHeight));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin(
        "##MPKeyboard", nullptr,
        ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());


    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 linePosition = ImGui::GetCursorScreenPos();
        const float lineHeight = UI::S(56.0f);
        const float inputWidth = displaySize.x - padding * 2;

        drawList->AddRectFilled(
            linePosition,
            ImVec2(linePosition.x + inputWidth,
                   linePosition.y + lineHeight),
            IM_COL32(20, 20, 20, 255), UI::S(6));
        drawList->AddRect(
            linePosition,
            ImVec2(linePosition.x + inputWidth,
                   linePosition.y + lineHeight),
            UI::kAccent, UI::S(6), 0, UI::S(2));

        std::string displayText = s_title + ": " + s_text;
        const bool showCaret =
            static_cast<int>(ImGui::GetTime() * 2.0) % 2 == 0;
        if (showCaret) {
            displayText += "_";
        }

        ImGui::PushFont(UI::Font());
        drawList->AddText(
            UI::Font(), UI::S(30),
            ImVec2(linePosition.x + UI::S(14),
                   linePosition.y + (lineHeight - UI::S(30)) * 0.5f),
            IM_COL32_WHITE, displayText.c_str());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(inputWidth, lineHeight + padding));
    }

    const char** keyboardRows =
        s_numericLayout
            ? kNumericRows
            : (s_symbolsEnabled
                   ? kSymbolRows
                   : (s_shiftEnabled ? kUppercaseRows : kLowercaseRows));
    const int maxColumns = s_numericLayout ? 3 : 10;
    const float availableWidth = displaySize.x - padding * 2;
    const float keySpacing = ImGui::GetStyle().ItemSpacing.x;
    const float keyWidth =
        (availableWidth - keySpacing * (maxColumns - 1)) / maxColumns;
    const float keyHeight =
        (keyboardHeight - UI::S(56.0f) - padding * 4) / 5.2f;

    ImGui::SetWindowFontScale(1.15f);
    for (int rowIndex = 0; rowIndex < 4; ++rowIndex) {
        const char* rowCharacters = keyboardRows[rowIndex];
        const int characterCount = static_cast<int>(strlen(rowCharacters));
        const float rowWidth =
            characterCount * keyWidth + (characterCount - 1) * keySpacing;

        ImGui::SetCursorPosX(
            padding + (availableWidth - rowWidth) * 0.5f);

        for (int columnIndex = 0; columnIndex < characterCount; ++columnIndex) {
            char keyLabel[8] = {rowCharacters[columnIndex], 0};
            snprintf(keyLabel, sizeof(keyLabel), "%c##k%d%d",
                     rowCharacters[columnIndex], rowIndex, columnIndex);

            if (DrawKeyboardKey(keyLabel, keyWidth, keyHeight)) {
                AppendCharacter(rowCharacters[columnIndex]);
            }

            if (columnIndex < characterCount - 1) {
                ImGui::SameLine();
            }
        }
    }


    ImGui::SetCursorPosX(padding);
    const float baseKeyWidth =
        (availableWidth - keySpacing * 5) / 10.0f;

    if (!s_numericLayout) {
        const char* shiftLabel = s_shiftEnabled ? "SHIFT##sh" : "Shift##sh";
        const ImU32 shiftColor = s_shiftEnabled ? UI::kAccent : 0;
        if (DrawKeyboardKey(shiftLabel, baseKeyWidth * 1.3f,
                            keyHeight, shiftColor)) {
            s_shiftEnabled = !s_shiftEnabled;
        }

        ImGui::SameLine();
        const char* symbolLabel = s_symbolsEnabled ? "ABC##sy" : "?123##sy";
        if (DrawKeyboardKey(symbolLabel, baseKeyWidth * 1.3f, keyHeight)) {
            s_symbolsEnabled = !s_symbolsEnabled;
        }

        ImGui::SameLine();
        if (DrawKeyboardKey("Space##sp", baseKeyWidth * 3.4f, keyHeight)) {
            AppendCharacter(' ');
        }
        ImGui::SameLine();
    } else {
        if (DrawKeyboardKey("Space##sp",
                            baseKeyWidth * 6.0f + keySpacing * 2,
                            keyHeight)) {
            AppendCharacter(' ');
        }
        ImGui::SameLine();
    }

    if (DrawKeyboardKey("<- Del##bk", baseKeyWidth * 1.3f, keyHeight,
                        IM_COL32(120, 60, 20, 255)) && !s_text.empty()) {
        s_text.pop_back();
    }

    ImGui::SameLine();
    if (DrawKeyboardKey("Enter##en", baseKeyWidth * 1.5f, keyHeight,
                        UI::kAccentDark)) {
        const std::string submittedText = s_text;
        VKeyboard::SubmitFn submitCallback = s_submitCallback;
        void* callbackData = s_callbackData;
        s_isOpen = false;

        if (submitCallback) {
            submitCallback(submittedText.c_str(), callbackData);
        }
    }

    ImGui::SameLine();
    if (DrawKeyboardKey("X##cl", baseKeyWidth * 1.2f - keySpacing,
                        keyHeight, IM_COL32(150, 30, 30, 255))) {
        s_isOpen = false;
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();
}
