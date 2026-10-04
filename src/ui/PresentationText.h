#pragma once
#include "i18n/Translator.h"
#include <array>
#include <string>
#include <string_view>

namespace lholo::ui {
inline char const* layerAxisDisplayName(int axis) noexcept {
    constexpr std::array keys{
        i18n::TextKey::ComboLayerAxisY,i18n::TextKey::ComboLayerAxisX,i18n::TextKey::ComboLayerAxisMaterial,
        i18n::TextKey::LayerBottomToTop,i18n::TextKey::LayerTopToBottom,
        i18n::TextKey::LayerWestToEast,i18n::TextKey::LayerEastToWest,
        i18n::TextKey::LayerNorthToSouth,i18n::TextKey::LayerSouthToNorth};
    return i18n::tr(keys[axis>=0&&axis<static_cast<int>(keys.size())?axis:0]);
}
inline std::string presentationStatus(std::string_view status) {
    if(status.empty()||i18n::languageCode(i18n::language())!="ja_JP")return std::string(status);
    constexpr std::pair<char const*,char const*> names[]{
        {"Session only: stable server address+port unavailable.","接続先を識別できないため、今回の接続中だけ保存・使用します。"},
        {"Verification placement is outside world bounds.","配置がワールドの範囲外です。位置を確認してください。"},
        {"Active placement changed before publication","読込み中に操作対象の配置が変わりました。再度選択してください。"},
        {"Too many placements","配置数が上限に達しています。不要な配置を削除してください。"},
        {"Cannot compare schematic import","設計図を取り込む前の確認に失敗しました。"},
        {"Schematic changed during import","取込み中に設計図が変更されました。再試行してください。"},
        {"Cannot read schematic import","取込み元の設計図を読み込めません。"},
        {"Schematic import size invalid","取込み元のファイルサイズが対応範囲外です。"},
        {"Schematic file name invalid","設計図のファイル名を確認してください。"},
        {"Cannot stage schematic import","取込み用の一時ファイルを保存できません。"},
        {"No free schematic import name","取込み先で使えるファイル名がありません。"}};
    for(auto const& [key,label]:names)if(status==key)return label;
    // OS/library details remain attributable; the Japanese prefix explains
    // their role without guessing an action or changing the state machine.
    return "状態の詳細: "+std::string(status);
}
}
