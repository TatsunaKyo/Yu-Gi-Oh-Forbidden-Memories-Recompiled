/* Field Fusion Helper: show fusion routes that start from a monster already
 * on the player's field and continue with cards in hand. */
#include "pc/mods/modapi.h"
#include "pc/cards/cards.h"
#include "pc/cards/fusion.h"
#include "pc/cards/rules.h"
#include "pc/cards/tables.h"
#include "game/card_constants.h"
#include "game/duel_card.h"
#include "game/duel_grid.h"
#include "game/duel_scene_state.h"
#define D_8009B360_AS_SIDE_ARRAY
#include "game/duel_side_state.h"
#include "game/duel_terrain_boost.h"
#include <stdio.h>
#include <string.h>

#define FIELD_SLOTS 5
#define MONSTER_ROW 2

typedef struct {
    int zone;
    FusionLine route;
} FieldResult;

static const MemoriesModHost *host;

static FusionCard base_card(int id)
{
    FusionCard result = {0};
    unsigned stats;
    if (!Cards_Valid(id)) return result;
    stats = (unsigned)gDuel_adwCardStats[id - 1];
    result.id = id;
    result.type = (int)(stats >> CARD_STAT_TYPE_SHIFT & CARD_STAT_TYPE_MASK);
    result.attack = (int)(stats & CARD_STAT_VALUE_MASK) * CARD_STAT_SCALE;
    result.defense = (int)(stats >> CARD_STAT_DEFENSE_SHIFT & CARD_STAT_VALUE_MASK) * CARD_STAT_SCALE;
    result.terrain = Duel_GetTerrainBoost(result.type);
    return result;
}

static FusionCard live_card(const DuelCardRecord *record)
{
    FusionCard result = {0};
    if (!record || !(record->flags & DUEL_CARD_FLAG_OCCUPIED) || !Cards_Valid(record->card_id)) return result;
    result.id = record->card_id;
    result.type = Cards_Type(record->card_id);
    result.attack = record->attack;
    result.defense = record->defense;
    result.modifier = record->stat_modifier;
    result.terrain = record->terrain_modifier;
    result.defense_modifier = record->defense_modifier;
    return result;
}

/* Keep speculative equip maths identical to the built-in Fusion Helper. */
static void bonuses(int equipment, int monster, int modifier, int defense_modifier,
                    int *attack, int *defense)
{
    int value, guard, had;
    int room = 2 * Tables_StatCapEither();
    Tables_EquipBoost(equipment, monster, &value, &guard);
    if (room > TABLES_LIMIT_STAT_MAX) room = TABLES_LIMIT_STAT_MAX;
    if (modifier + value > room) value = room - modifier;
    if (modifier + value < -room) value = -room - modifier;
    had = modifier + defense_modifier;
    if (had + guard > room) guard = room - had;
    if (had + guard < -room) guard = -room - had;
    if (defense_modifier + guard - value > TABLES_LIMIT_STAT_MAX)
        guard = TABLES_LIMIT_STAT_MAX - defense_modifier + value;
    if (defense_modifier + guard - value < -TABLES_LIMIT_STAT_MAX)
        guard = -TABLES_LIMIT_STAT_MAX - defense_modifier + value;
    *attack = value;
    *defense = guard;
}

static const FusionRules rules = {base_card, CardRules_Fusion, CardRules_Equip, NULL, bonuses};

static int better(const FusionLine *candidate, const FusionLine *best)
{
    int attack, best_attack, defense, best_defense;
    if (!candidate->count) return 0;
    if (!best->count) return 1;
    attack = Fusion_Attack(candidate->card);
    best_attack = Fusion_Attack(best->card);
    if (attack != best_attack) return attack > best_attack;
    if (candidate->count != best->count) return candidate->count < best->count;
    defense = Fusion_Defense(candidate->card);
    best_defense = Fusion_Defense(best->card);
    return defense > best_defense;
}

static void search_route(const FusionCard hand[FUSION_HAND], FusionCard current,
                         unsigned used, FusionLine line, FusionLine *best)
{
    int slot;
    if (line.count >= FUSION_HAND) return;
    for (slot = 0; slot < FUSION_HAND; slot++) {
        FusionLine next = line;
        if ((used & (1u << slot)) || !hand[slot].id) continue;
        if (!Fusion_Step(&rules, current, hand[slot], &next.card)) continue;
        next.slots[next.count++] = slot;
        if (next.card.type < CARD_TYPE_MAGIC && better(&next, best)) *best = next;
        search_route(hand, next.card, used | (1u << slot), next, best);
    }
}

static FusionLine best_route(FusionCard field, const FusionCard hand[FUSION_HAND])
{
    FusionLine line = {0}, best = {0};
    line.card = field;
    search_route(hand, field, 0, line, &best);
    return best;
}

static int read_hand(int side, FusionCard hand[FUSION_HAND])
{
    int slot, count = 0;
    memset(hand, 0, sizeof(FusionCard) * FUSION_HAND);
    for (slot = 0; slot < FUSION_HAND; slot++) {
        int index = D_800907CC[side * FUSION_HAND + slot];
        if (index < 0 || index >= DUEL_CARD_RECORD_COUNT) continue;
        hand[slot] = live_card(&D_801A7AD8[index]);
        if (hand[slot].id) count++;
    }
    return count;
}

static int collect_results(int side, const FusionCard hand[FUSION_HAND], FieldResult out[FIELD_SLOTS])
{
    int col, count = 0;
    for (col = 0; col < FIELD_SLOTS; col++) {
        int grid = side * DUEL_FIELD_SIDE_GRID_SLOT_COUNT + MONSTER_ROW * DUEL_FIELD_ROW_SIZE + col;
        int index = D_800907D8[grid];
        FusionCard field;
        FusionLine route;
        if (index < 0 || index >= DUEL_CARD_RECORD_COUNT) continue;
        field = live_card(&D_801A7AD8[index]);
        if (!field.id || field.type >= CARD_TYPE_MAGIC) continue;
        route = best_route(field, hand);
        if (!route.count) continue;
        out[count].zone = col;
        out[count].route = route;
        count++;
    }
    return count;
}

static void card_name(int id, char out[28])
{
    char full[256];
    size_t i, j = 0;
    full[0] = 0;
    if (!Cards_Valid(id) || !Cards_NameUtf8(id, full, sizeof(full)) || !full[0]) {
        snprintf(out, 28, "Card %d", id);
        return;
    }
    for (i = 0; full[i] && j < 24; i++) {
        unsigned char c = (unsigned char)full[i];
        out[j++] = (c >= 32 && c <= 126) ? (char)c : '?';
    }
    if (full[i] && j >= 3) {
        out[j - 3] = '.'; out[j - 2] = '.'; out[j - 1] = '.';
    }
    out[j] = 0;
}

static void route_text(const FusionLine *route, char *out, size_t size)
{
    int i, used = 0;
    out[0] = 0;
    for (i = 0; i < route->count; i++) {
        int wrote = snprintf(out + used, size - (size_t)used, "%sH%d", i ? ">" : "", route->slots[i] + 1);
        if (wrote < 0 || wrote >= (int)(size - (size_t)used)) break;
        used += wrote;
    }
}

static int text_scale(int ui_scale)
{
    int value = host->setting(host, "text_scale", 3);
    if (ui_scale < 1) ui_scale = 1;
    if (value >= 1 && value <= 6) return value;
    return ui_scale;
}

static int should_draw(void)
{
    int side = D_8009B1D5;
    if (side >= DUEL_SIDE_COUNT) return 0;
    if ((gDuel_wSceneStateFlags & DUEL_SCENE_PHASE_MASK) != 4) return 0;
    return D_8009B360[side] < 0;
}

static void overlay(void)
{
    FusionCard hand[FUSION_HAND];
    FieldResult results[FIELD_SLOTS];
    int width, height, ui_scale, scale, header_scale;
    int count, i, style, position, show_empty;
    int pad, gap, border, header_h, row_h, menu_h;
    int panel_w, panel_h, x, y, title_w;
    const char *title = "FIELD FUSIONS";

    if (!host || !should_draw()) return;
    if (!read_hand(D_8009B1D5, hand)) return;
    Fusion_SetCaps(Tables_StatCap(0), Tables_StatCap(1));
    count = collect_results(D_8009B1D5, hand, results);
    show_empty = host->setting(host, "show_empty", 0);
    if (!count && !show_empty) return;

    host->overlay_size(host, &width, &height, &ui_scale);
    if (width <= 0 || height <= 0) return;
    scale = text_scale(ui_scale);
    if (scale < 1) scale = 1;
    if (scale > 6) scale = 6;
    header_scale = scale > 1 ? scale - 1 : 1;
    style = host->setting(host, "style", 0);
    position = host->setting(host, "position", 0);

    pad = 3 * scale;
    gap = 4 * scale;
    border = scale > 2 ? 2 : 1;
    header_h = 16 * header_scale + 2 * pad;
    row_h = 18 * scale;
    /* The port reports its own menu scale separately from text size. */
    menu_h = 22 * (ui_scale < 1 ? 1 : ui_scale);

    title_w = host->text_width(host, title, header_scale) + 2 * pad;
    panel_w = title_w;
    if (!count) {
        int w = host->text_width(host, "No field fusion", scale) + 2 * pad;
        if (w > panel_w) panel_w = w;
    } else {
        for (i = 0; i < count; i++) {
            char field[8], route[40], name[28], stats[24];
            int row_w;
            snprintf(field, sizeof(field), "F%d", results[i].zone + 1);
            route_text(&results[i].route, route, sizeof(route));
            card_name(results[i].route.card.id, name);
            snprintf(stats, sizeof(stats), "%d/%d", Fusion_Attack(results[i].route.card), Fusion_Defense(results[i].route.card));
            row_w = host->text_width(host, field, scale) + gap
                  + host->text_width(host, route, scale) + gap
                  + host->text_width(host, ">", scale) + gap
                  + host->text_width(host, name, scale) + gap
                  + host->text_width(host, stats, scale) + 2 * pad;
            if (row_w > panel_w) panel_w = row_w;
        }
    }

    panel_h = header_h + (count ? count : 1) * row_h + 2 * border;
    if (panel_w > width - 2 * pad) panel_w = width - 2 * pad;
    if (position == 1) x = (width - panel_w) / 2;
    else if (position == 2) x = width - panel_w - pad;
    else x = pad;
    if (x < pad) x = pad;
    y = menu_h + 2 * (ui_scale < 1 ? 1 : ui_scale);
    if (y + panel_h > height) y = pad;

    if (style == 0) {
        host->fill(host, x, y, panel_w, panel_h, 0x080b10u, 210);
        host->fill(host, x, y, panel_w, border, 0x78c99au, 235);
        host->fill(host, x, y + panel_h - border, panel_w, border, 0x303741u, 220);
        host->fill(host, x, y, border, panel_h, 0x303741u, 220);
        host->fill(host, x + panel_w - border, y, border, panel_h, 0x303741u, 220);
        host->fill(host, x + border, y + border, panel_w - 2 * border, header_h - border, 0x151a21u, 225);
    }

    host->draw_text(host, x + pad, y + header_h / 2, title, 0xf3f5f7u, header_scale);
    if (!count) {
        host->draw_text(host, x + pad, y + header_h + row_h / 2, "No field fusion", 0x9aa3adu, scale);
        return;
    }

    for (i = 0; i < count; i++) {
        char field[8], route[40], name[28], stats[24];
        int middle = y + header_h + row_h * i + row_h / 2;
        int cursor = x + pad;
        int w;
        snprintf(field, sizeof(field), "F%d", results[i].zone + 1);
        route_text(&results[i].route, route, sizeof(route));
        card_name(results[i].route.card.id, name);
        snprintf(stats, sizeof(stats), "%d/%d", Fusion_Attack(results[i].route.card), Fusion_Defense(results[i].route.card));
        if (style == 0 && (i & 1)) host->fill(host, x + border, middle - row_h / 2, panel_w - 2 * border, row_h, 0x11161du, 120);
        host->draw_text(host, cursor, middle, field, 0xe1bf63u, scale);
        w = host->text_width(host, field, scale); cursor += w + gap;
        host->draw_text(host, cursor, middle, route, 0xd7dde4u, scale);
        w = host->text_width(host, route, scale); cursor += w + gap;
        host->draw_text(host, cursor, middle, ">", 0x78c99au, scale);
        w = host->text_width(host, ">", scale); cursor += w + gap;
        host->draw_text(host, cursor, middle, name, 0xaee8bfu, scale);
        w = host->text_width(host, name, scale); cursor += w + gap;
        host->draw_text(host, cursor, middle, stats, 0xb9c1c9u, scale);
    }
}

int MemoriesModInit(const MemoriesModHost *from, MemoriesMod *mod)
{
    if (!from || !mod || from->api < 4) return 0;
    host = from;
    mod->api = 4;
    mod->name = "Field Fusion Helper";
    mod->overlay = overlay;
    return 1;
}
