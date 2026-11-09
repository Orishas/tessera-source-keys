#include "source_keys.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace esphome::source_keys {

using tessera::Font;
namespace ui = tessera::ui;

// Where the logos live: this plugin's own repository, one PNG per logo and look (light and dark).
static const char *const LOGO_BASE = "https://raw.githubusercontent.com/Orishas/tessera-source-keys/main/logos/";
static constexpr int MAX_KEYS = 6;
// How long a tapped key stays chosen before Home Assistant has said so, in milliseconds.
static constexpr uint32_t PENDING_MS = 15000;

// The logo a word stands for, when the person gave none: "PC" is Windows, "Mac" Apple, and so on.
static std::string guess_logo(const std::string &label) {
  std::string l;
  for (char ch : label) l += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  auto has = [&l](const char *w) { return l.find(w) != std::string::npos; };
  if (has("mac") || has("apple")) return "apple";
  if (has("ps") || has("playstation")) return "playstation";
  if (has("switch") || has("nintendo")) return "nintendo-switch";
  if (has("xbox")) return "xbox";
  if (has("pc") || has("windows")) return "windows";
  if (has("linux")) return "linux";
  return "";
}

// One key: what it chooses, the word under it and the logo over it.
struct Key {
  char value[65];
  char logo[33];
};

// The tile: a row of keys for the options of a select. The key of the option that is chosen now is the accent.
// A tap chooses the option of its key: with a script set, the script runs with the option as a field (so it can wake
// the monitor and switch the input); without, the select itself takes the option.
class KeysTile : public tessera::Tile {
 public:
  explicit KeysTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    parent_ = c.parent;
    width_ = c.width;
    height_ = c.height;
    entity_ = roomy::text(std::string(c.entity ? c.entity : ""));
    script_ = roomy::text(std::string(c.options["script"] | ""));
    field_ = roomy::text(std::string(c.options["field"] | "quelle"));
    labels_ = c.options["labels"] | true;
    // The keys the person listed, as "Value|logo" or "Value"; empty: the select's own options, read in on_state.
    for (JsonVariantConst v : c.options["keys"].as<JsonArrayConst>()) {
      if (count_ >= MAX_KEYS) break;
      add_key(v.as<const char *>() ? v.as<const char *>() : "");
    }
    listed_ = count_ > 0;

    for (int i = 0; i < MAX_KEYS; ++i) {
      block_[i] = ui::block(c.parent, theme::TRACK);
      place_[i] = ui::pictures() ? ui::picture(c.parent, tessera::Placeholder::NONE) : nullptr;
      // The word lies in its key's block, so it moves and colours with it.
      label_[i] = ui::label(block_[i], Font::BODY, theme::MUTED);
      lv_obj_set_style_pad_all(block_[i], 0, 0);
      lv_obj_remove_flag(block_[i], LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_text_align(label_[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(note_, width_);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note_, 0, (height_ - ui::line_height(Font::BODY)) / 2);
    dark_ = is_dark();
    layout();
  }

  void on_state(JsonObjectConst data) override {
    has_state_ = !data["state"].isNull();
    chosen_ = roomy::text(std::string(data["state"] | ""));
    if (!listed_) {
      count_ = 0;
      for (JsonVariantConst v : data["attributes"]["options"].as<JsonArrayConst>()) {
        if (count_ >= MAX_KEYS) break;
        add_key(v.as<const char *>() ? v.as<const char *>() : "");
      }
    }
    // Home Assistant has the tapped option now, or it never will: the key follows the select again.
    if (pending_ >= 0 && chosen_ == keys_[pending_].value) pending_ = -1;
    layout();
  }

  void on_tick(uint32_t) override {
    if (pending_ >= 0 && esphome::millis() - pending_at_ > PENDING_MS) {
      pending_ = -1;
      paint();
    }
  }

  void on_theme() override {
    dark_ = is_dark();
    paint();
  }

  void on_tap() override {
    if (count_ == 0 || entity_.empty()) return;
    const tessera::Point p = tessera::tap_point();
    if (p.x < 0) return;
    const int i = std::min(count_ - 1, std::max(0, p.x * count_ / std::max(1, width_)));
    const std::string value = keys_[i].value;
    bool sent;
    if (!script_.empty()) {
      JsonDocument d;
      d["variables"][roomy::plain(field_)] = value;
      sent = tessera::action("script.turn_on", roomy::plain(script_), d);
    } else {
      const std::string entity = roomy::plain(entity_);
      JsonDocument d;
      d["option"] = value;
      sent = tessera::action(entity.rfind("select.", 0) == 0 ? "select.select_option" : "input_select.select_option",
                             entity, d);
    }
    if (sent) {
      pending_ = i;
      pending_at_ = esphome::millis();
      paint();
    }
  }

 private:
  void add_key(const char *text) {
    std::string s = text;
    std::string value = s, logo;
    const size_t bar = s.find('|');
    if (bar != std::string::npos) {
      value = s.substr(0, bar);
      logo = s.substr(bar + 1);
    }
    if (value.empty()) return;
    if (logo.empty()) logo = guess_logo(value);
    Key &k = keys_[count_++];
    snprintf(k.value, sizeof(k.value), "%s", value.c_str());
    snprintf(k.logo, sizeof(k.logo), "%s", logo.c_str());
  }

  // The look is dark when the card is: the logos come in a light and a dark colour.
  bool is_dark() const { return lv_color_brightness(ui::color(theme::CARD)) < 128; }

  // The key that shows as chosen: the one just tapped, else the select's option.
  int active() const {
    if (pending_ >= 0) return pending_;
    for (int i = 0; i < count_; ++i)
      if (chosen_ == keys_[i].value) return i;
    return -1;
  }

  // Places every key: as wide as the card allows, a gap between them, the logo over the word.
  void layout() {
    const int gap = ui::px(8);
    const int n = std::max(1, count_);
    const int kw = (width_ - gap * (n - 1)) / n;
    const int lh = labels_ ? ui::line_height(Font::BODY) : 0;
    const int pad = ui::px(8);
    const int side = std::max(0, std::min(kw - 2 * pad, height_ - lh - 3 * pad));
    for (int i = 0; i < MAX_KEYS; ++i) {
      const bool shown = i < count_;
      for (lv_obj_t *o : {block_[i], place_[i], label_[i]}) {
        if (!o) continue;
        if (shown) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      }
      if (!shown) continue;
      const int x = i * (kw + gap);
      lv_obj_set_pos(block_[i], x, 0);
      lv_obj_set_size(block_[i], kw, height_);
      const int top = (height_ - side - (lh ? pad + lh : 0)) / 2;
      if (place_[i]) {
        lv_obj_set_pos(place_[i], x + (kw - side) / 2, top);
        lv_obj_set_size(place_[i], side, side);
      }
      lv_obj_set_pos(label_[i], 0, place_[i] ? top + side + pad : (height_ - lh) / 2);
      lv_obj_set_width(label_[i], kw);
      if (!labels_ && place_[i]) lv_obj_add_flag(label_[i], LV_OBJ_FLAG_HIDDEN);
      ui::set_text(label_[i], keys_[i].value);
    }
    paint();
  }

  // Colours and logos: the chosen key on the accent, the others on the track.
  void paint() {
    const int on = active();
    for (int i = 0; i < count_; ++i) {
      const bool chosen = i == on;
      ui::set_fill(block_[i], tessera::Tone::NORMAL, chosen ? theme::ACCENT : theme::TRACK);
      ui::set_color(label_[i], chosen ? theme::ON_ACCENT : theme::INK_SOFT);
      if (place_[i]) {
        std::string url;
        if (keys_[i].logo[0]) url = std::string(LOGO_BASE) + keys_[i].logo + (chosen || dark_ ? "-light.png" : "-dark.png");
        ui::set_picture(place_[i], url, tessera::Fit::CONTAIN);
      }
    }
    // Nothing to show: say what is missing.
    std::string note;
    if (entity_.empty()) note = plugin_->text("choose_select");
    else if (count_ == 0) note = plugin_->text(has_state_ ? "no_options" : "waiting");
    ui::set_text(note_, note);
    if (note.empty()) lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
    ui::set_color(note_, theme::MUTED);
  }

  const tessera::Plugin *plugin_;
  lv_obj_t *parent_{};
  int width_ = 0, height_ = 0, count_ = 0, pending_ = -1;
  uint32_t pending_at_ = 0;
  bool listed_ = false, labels_ = true, dark_ = false, has_state_ = false;
  tessera::Text entity_, script_, field_, chosen_;   // PSRAM first (docs/FIRMWARE_API.md, "Memory")
  Key keys_[MAX_KEYS]{};
  lv_obj_t *block_[MAX_KEYS]{}, *place_[MAX_KEYS]{}, *label_[MAX_KEYS]{}, *note_{};
};

void SourceKeys::setup() {
  add_tile("keys", [this]() { return new KeysTile(this); });
}

}  // namespace esphome::source_keys
