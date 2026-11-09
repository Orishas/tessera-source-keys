#include "source_keys.h"
#include "logos.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>

namespace esphome::source_keys {

using tessera::Font;
namespace ui = tessera::ui;

static constexpr int MAX_KEYS = 6;
// How long a tapped key stays chosen before Home Assistant has said so, in milliseconds.
static constexpr uint32_t PENDING_MS = 15000;
// Icons of Tessera's set for the monitor's row.
static constexpr uint32_t ICON_EYE = 0xF0208;
static constexpr uint32_t ICON_TV_OFF = 0xF083B;
static constexpr uint32_t ICON_POWER = 0xF0425;

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

// The mask of a logo by its name (logos.h), or none.
static const uint8_t *logo_alpha(const char *name) {
  for (const Logo &l : LOGOS)
    if (strcmp(l.name, name) == 0) return l.alpha;
  return nullptr;
}

// A mask of LOGO_SIZE pixels a side, made `side` pixels a side: each pixel the mean of the source pixels it covers
// (bilinear when it grows). LVGL then draws it as it is, without a transform of its own.
static void resample(const uint8_t *src, int side, uint8_t *dst) {
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x) {
      const float x0 = float(x) * LOGO_SIZE / side, x1 = float(x + 1) * LOGO_SIZE / side;
      const float y0 = float(y) * LOGO_SIZE / side, y1 = float(y + 1) * LOGO_SIZE / side;
      float sum = 0, area = 0;
      for (int sy = int(y0); sy < int(std::ceil(y1)) && sy < LOGO_SIZE; ++sy)
        for (int sx = int(x0); sx < int(std::ceil(x1)) && sx < LOGO_SIZE; ++sx) {
          const float w = (std::min(x1, float(sx + 1)) - std::max(x0, float(sx))) *
                          (std::min(y1, float(sy + 1)) - std::max(y0, float(sy)));
          sum += w * src[sy * LOGO_SIZE + sx];
          area += w;
        }
      dst[y * side + x] = area > 0 ? uint8_t(sum / area + 0.5f) : 0;
    }
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
    name_ = roomy::text(std::string(c.name ? c.name : ""));
    picture_ = roomy::text(std::string(c.options["picture"] | ""));
    power_ = roomy::text(std::string(c.options["power"] | ""));
    // The monitor's row: a head line with its state, and keys for the picture and the power under the sources.
    // Only on a tile two rows high, and only when a switch for one of them is chosen.
    monitor_ = c.rows >= 2 && (!picture_.empty() || !power_.empty());
    head_on_ = c.options["head"] | false;
    row_words_ = c.options["row_words"] | true;
    // How much of the tile's height the monitor's row takes: small, medium or large.
    const std::string size = c.options["row_size"] | "small";
    row_pct_ = size == "large" ? 48 : size == "medium" ? 38 : 27;
    // The keys the person listed, as "Value|logo" or "Value"; empty: the select's own options, read in on_state.
    for (JsonVariantConst v : c.options["keys"].as<JsonArrayConst>()) {
      if (count_ >= MAX_KEYS) break;
      add_key(v.as<const char *>() ? v.as<const char *>() : "");
    }
    listed_ = count_ > 0;

    for (int i = 0; i < MAX_KEYS; ++i) {
      block_[i] = ui::block(c.parent, theme::TRACK);
      lv_obj_set_style_pad_all(block_[i], 0, 0);
      lv_obj_remove_flag(block_[i], LV_OBJ_FLAG_SCROLLABLE);
      // The logo and the word lie in their key's block, so they move and colour with it.
      logo_[i] = lv_image_create(block_[i]);
      lv_obj_set_style_image_recolor_opa(logo_[i], LV_OPA_COVER, 0);
      lv_obj_remove_flag(logo_[i], LV_OBJ_FLAG_CLICKABLE);
      label_[i] = ui::label(block_[i], Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(label_[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    if (monitor_) {
      head_ = ui::label(c.parent, Font::BODY, theme::INK_SOFT);
      status_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_RIGHT, 0);
      for (int k = 0; k < 2; ++k) {
        mblock_[k] = ui::block(c.parent, theme::TRACK);
        lv_obj_set_style_pad_all(mblock_[k], 0, 0);
        lv_obj_remove_flag(mblock_[k], LV_OBJ_FLAG_SCROLLABLE);
        micon_[k] = ui::label(mblock_[k], Font::ICON_SMALL, theme::INK);
        mlabel_[k] = ui::label(mblock_[k], Font::BODY, theme::INK_SOFT);
      }
    }
    note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(note_, width_);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note_, 0, (height_ - ui::line_height(Font::BODY)) / 2);
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
    picture_on_ = std::string(data["entities"]["picture"]["state"] | "") == "on";
    power_on_ = power_.empty() ? true : std::string(data["entities"]["power"]["state"] | "") == "on";
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

  void on_theme() override { paint(); }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (p.x < 0) return;
    if (monitor_ && p.y >= row_y_) return tap_monitor(p.x);
    if (p.y < keys_y_ || count_ == 0 || entity_.empty()) return;
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
  // The monitor's row: one key that turns it on while it is off, else the picture's key and the power's key.
  bool single_power() const { return !power_.empty() && !power_on_; }

  void tap_monitor(int x) {
    const bool left = !single_power() && !picture_.empty() && (power_.empty() || x < width_ / 2);
    if (left) {
      tessera::action(picture_on_ ? "switch.turn_off" : "switch.turn_on", roomy::plain(picture_));
    } else if (!power_.empty()) {
      tessera::action(power_on_ ? "switch.turn_off" : "switch.turn_on", roomy::plain(power_));
    }
  }

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
    if (monitor_) {
      const int head = head_on_ ? ui::line_height(Font::BODY) + gap : 0;
      row_h_ = std::max(ui::line_height(Font::BODY) * 3 / 2, height_ * row_pct_ / 100);
      keys_y_ = head;
      keys_h_ = std::max(0, height_ - keys_y_ - gap - row_h_);
      row_y_ = height_ - row_h_;
      if (head_on_) {
        lv_obj_remove_flag(head_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(status_, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(head_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN);
      }
      lv_obj_set_pos(head_, 0, 0);
      lv_obj_set_width(head_, width_ / 2);
      lv_obj_set_pos(status_, width_ / 2, 0);
      lv_obj_set_width(status_, width_ - width_ / 2);
    } else {
      keys_y_ = 0;
      keys_h_ = height_;
    }
    const int n = std::max(1, count_);
    const int kw = (width_ - gap * (n - 1)) / n;
    const int lh = labels_ ? ui::line_height(Font::BODY) : 0;
    const int pad = ui::px(8);
    const int side = std::max(0, std::min(kw - 2 * pad, keys_h_ - lh - 3 * pad));
    for (int i = 0; i < MAX_KEYS; ++i) {
      const bool shown = i < count_;
      for (lv_obj_t *o : {block_[i]}) {
        if (!o) continue;
        if (shown) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      }
      if (!shown) continue;
      const int x = i * (kw + gap);
      lv_obj_set_pos(block_[i], x, keys_y_);
      lv_obj_set_size(block_[i], kw, keys_h_);
      // A key without a logo shows its word in the middle, always.
      const uint8_t *alpha = keys_[i].logo[0] && side > 0 ? logo_alpha(keys_[i].logo) : nullptr;
      const bool image = alpha != nullptr;
      const bool word = labels_ || !image;
      const int wh = word ? ui::line_height(Font::BODY) : 0;
      const int top = (keys_h_ - (image ? side : 0) - (image && word ? pad : 0) - wh) / 2;
      if (image) {
        // The mask at the key's size, made again only when the size or the logo changed.
        if (made_side_[i] != side || made_logo_[i] != alpha) {
          mask_[i].assign(size_t(side) * side, 0);
          resample(alpha, side, mask_[i].data());
          lv_image_set_src(logo_[i], nullptr);
          lv_image_dsc_t &d = image_[i];
          d = lv_image_dsc_t{};
          d.header.magic = LV_IMAGE_HEADER_MAGIC;
          d.header.cf = LV_COLOR_FORMAT_A8;
          d.header.w = side;
          d.header.h = side;
          d.header.stride = side;
          d.data = mask_[i].data();
          d.data_size = mask_[i].size();
          made_side_[i] = side;
          made_logo_[i] = alpha;
          lv_image_set_src(logo_[i], &image_[i]);
          lv_obj_invalidate(logo_[i]);
        }
        lv_obj_set_pos(logo_[i], (kw - side) / 2, top);
        lv_obj_set_size(logo_[i], side, side);
        lv_obj_remove_flag(logo_[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(logo_[i], LV_OBJ_FLAG_HIDDEN);
      }
      lv_obj_set_pos(label_[i], 0, image ? top + side + pad : top);
      lv_obj_set_width(label_[i], kw);
      if (word) lv_obj_remove_flag(label_[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(label_[i], LV_OBJ_FLAG_HIDDEN);
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
      lv_obj_set_style_image_recolor(logo_[i], ui::color(chosen ? theme::ON_ACCENT : theme::INK), 0);
    }
    if (monitor_) paint_monitor();
    // Nothing to show: say what is missing.
    std::string note;
    if (entity_.empty()) note = plugin_->text("choose_select");
    else if (count_ == 0) note = plugin_->text(has_state_ ? "no_options" : "waiting");
    ui::set_text(note_, note);
    if (note.empty()) lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
    ui::set_color(note_, theme::MUTED);
  }

  // The head line, and the picture's and the power's keys, as the switches are now.
  void paint_monitor() {
    ui::set_text(head_, name_.empty() ? plugin_->text("monitor") : roomy::plain(name_));
    std::string status = plugin_->text("status_on");
    if (single_power()) status = plugin_->text("status_off");
    else if (!picture_.empty() && !picture_on_) status = plugin_->text("status_picture_off");
    else if (!chosen_.empty()) status += " · " + roomy::plain(chosen_);
    ui::set_text(status_, status);
    const int gap = ui::px(8);
    const bool one = single_power() || picture_.empty() || power_.empty();
    const int kw = one ? width_ : (width_ - gap) / 2;
    for (int k = 0; k < 2; ++k) {
      // Key 0 is the picture's, key 1 the power's; a row of one key shows the one that matters now.
      bool shown = !one || (k == 1 ? (!power_.empty() && (single_power() || picture_.empty())) : (!picture_.empty() && power_.empty()));
      if (!shown) {
        lv_obj_add_flag(mblock_[k], LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_remove_flag(mblock_[k], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(mblock_[k], one ? 0 : k * (kw + gap), row_y_);
      lv_obj_set_size(mblock_[k], kw, row_h_);
      uint32_t icon;
      std::string word;
      bool accent = false;
      if (k == 0) {
        icon = picture_on_ ? ICON_TV_OFF : ICON_EYE;
        word = plugin_->text(picture_on_ ? "picture_off" : "picture_on");
        accent = !picture_on_;
      } else {
        icon = ICON_POWER;
        word = plugin_->text(power_on_ ? "turn_off" : "turn_on");
        accent = !power_on_;
      }
      ui::set_fill(mblock_[k], tessera::Tone::NORMAL, accent ? theme::ACCENT : theme::TRACK);
      ui::set_text(micon_[k], ui::icon(icon));
      ui::set_text(mlabel_[k], word);
      ui::set_color(micon_[k], accent ? theme::ON_ACCENT : theme::INK);
      ui::set_color(mlabel_[k], accent ? theme::ON_ACCENT : theme::INK_SOFT);
      // The icon and the word together in the middle of the key.
      // A tall row takes the large icon and the larger words.
      const bool roomy_row = row_h_ >= ui::line_height(Font::ICON) + 2 * gap;
      const Font ifont = roomy_row ? Font::ICON : Font::ICON_SMALL;
      const Font tfont = roomy_row ? Font::BODY_LARGE : Font::BODY;
      ui::set_font(micon_[k], ifont);
      ui::set_font(mlabel_[k], tfont);
      const int iw = ui::line_height(ifont), space = ui::px(roomy_row ? 12 : 8);
      if (!row_words_) {
        // Only the icon, large, in the middle of the key.
        ui::set_font(micon_[k], Font::ICON);
        lv_obj_add_flag(mlabel_[k], LV_OBJ_FLAG_HIDDEN);
        const int big = ui::line_height(Font::ICON);
        lv_obj_set_pos(micon_[k], (kw - big) / 2, (row_h_ - big) / 2);
        continue;
      }
      lv_obj_remove_flag(mlabel_[k], LV_OBJ_FLAG_HIDDEN);
      const int tw = std::min(kw - iw - space - 2 * gap, ui::text_width(word, tfont));
      const int x0 = std::max(0, (kw - iw - space - tw) / 2);
      lv_obj_set_pos(micon_[k], x0, (row_h_ - ui::line_height(ifont)) / 2);
      lv_obj_set_pos(mlabel_[k], x0 + iw + space, (row_h_ - ui::line_height(tfont)) / 2);
      lv_obj_set_width(mlabel_[k], std::max(1, tw));
    }
  }

  const tessera::Plugin *plugin_;
  lv_obj_t *parent_{};
  int width_ = 0, height_ = 0, count_ = 0, pending_ = -1;
  uint32_t pending_at_ = 0;
  bool listed_ = false, labels_ = true, has_state_ = false;
  bool monitor_ = false, picture_on_ = false, power_on_ = true, head_on_ = false, row_words_ = true;
  int keys_y_ = 0, keys_h_ = 0, row_y_ = 0, row_h_ = 0, row_pct_ = 27;
  tessera::Text name_, picture_, power_;
  lv_obj_t *head_{}, *status_{}, *mblock_[2]{}, *micon_[2]{}, *mlabel_[2]{};
  tessera::Text entity_, script_, field_, chosen_;   // PSRAM first (docs/FIRMWARE_API.md, "Memory")
  Key keys_[MAX_KEYS]{};
  tessera::List<uint8_t> mask_[MAX_KEYS];   // each logo at its key's size (PSRAM)
  lv_image_dsc_t image_[MAX_KEYS]{};
  int made_side_[MAX_KEYS]{};
  const uint8_t *made_logo_[MAX_KEYS]{};
  lv_obj_t *block_[MAX_KEYS]{}, *logo_[MAX_KEYS]{}, *label_[MAX_KEYS]{}, *note_{};
};

void SourceKeys::setup() {
  add_tile("keys", [this]() { return new KeysTile(this); });
}

}  // namespace esphome::source_keys
