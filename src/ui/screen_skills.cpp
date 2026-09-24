// =============================================================================
//  The Game class's screens, continued: skills, the tree, the spellbook and the boons
//
//  One class in several files, cut along the banners screens.cpp always had:
//  screens.cpp keeps the menus; the rest is screen_hud, screen_inventory,
//  screen_skills, screen_journal, screen_talk and screen_trade. What two of
//  them share is in screens_shared.h.
// =============================================================================
#include "../systems/gathering.h"
#include "../game.h"
#include "screens_shared.h"

// =============================================================================
//  Skills
// =============================================================================

// =============================================================================
//  Milestones: what a skill's levels are actually for
// =============================================================================

// Everything the game asks this skill for, and the level it asks at. It is
// gathered from the things themselves rather than written out anywhere -- the
// tiers, the recipes, the spells, the character's own tree -- so a new tier or
// a new spell appears here the day it is added and cannot be forgotten.
vector<Game::SkillMilestone> Game::MilestonesFor(int skill) const {
    const Player& p = world->player;
    vector<SkillMilestone> out;
    const auto add = [&](int level, const string& text) {
        if (text.empty()) return;
        level = std::clamp(level, 1, MAX_SKILL_LEVEL);
        for (const auto& m : out)
            if (m.level == level && m.text == text) return;
        out.push_back({level, text});
    };
    const auto join = [](const vector<string>& parts, bool with_and) {
        string s;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) s += (with_and && i + 1 == parts.size()) ? " and " : ", ";
            s += parts[i];
        }
        return s;
    };
    const auto capitalised = [](string s) {
        if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
        return s;
    };

    // --- the character's own tree ----------------------------------------------
    // Theirs only. The page is their path's, and a hero has no use for a row
    // of the wayfarer's they will never be offered.
    const AttackStyle path = p.talents.HasPath() ? p.talents.Path() : p.Affinity();
    const TalentTree& tree = skill_trees.Tree(path);
    if (tree.skill == skill)
        for (const TalentNode& n : tree.nodes)
            add(n.level, n.name + (n.ranks > 1 ? " (" + std::to_string(n.ranks) + " ranks)" : string("")));

    // --- what it lets you hold ---------------------------------------------------
    // A tier's pieces all ask the same level, so they go on one line: "Mithril
    // bows and hides", not seven rows of mithril. What to call them is decided
    // by the skill doing the asking, which is how the data is built -- plate
    // asks Defence, hides Ranged, robes Magic.
    const auto worn_noun = [&](const ItemDef& d) -> string {
        if (!d.tool.empty()) return d.tool == "pickaxe" ? "pickaxes" : d.tool + "s";
        if (d.piece == "bar") return "bars";
        if (d.slot == SLOT_SHIELD) return "shields";
        if (d.slot == SLOT_WEAPON) {
            if (skill == SKILL_RANGED) return "bows";
            if (skill == SKILL_MAGIC)  return "staves";
            return "weapons";
        }
        if (skill == SKILL_RANGED)  return "hides";
        if (skill == SKILL_MAGIC)   return "robes";
        if (skill == SKILL_DEFENCE) return "plate";
        return "armour";
    };
    // (level, tier id) -> the nouns at it, in the order they were met. The
    // empty tier is everything the tiers did not make, which is listed by name.
    std::map<std::pair<int, string>, vector<string>> gear;
    const auto note = [&](std::map<std::pair<int, string>, vector<string>>& into,
                          int level, const string& tier, const string& noun) {
        vector<string>& v = into[{level, tier}];
        if (std::find(v.begin(), v.end(), noun) == v.end()) v.push_back(noun);
    };
    for (const auto& kv : items.All()) {
        const ItemDef& d = kv.second;
        // An enchanted twin is the piece it was worked from with a charm on
        // it: it asks nothing the piece did not, and there are hundreds.
        if (d.id.find('+') != string::npos) continue;
        const auto req = d.requirements.find(skill);
        if (req == d.requirements.end()) continue;
        note(gear, req->second, d.tier, d.tier.empty() ? d.name : worn_noun(d));
    }

    // --- and what it lets you make ------------------------------------------------
    // A tier's pieces are not all made at one level -- a wooden shield comes
    // after a wooden bow -- and the same noun would otherwise be written out
    // at four levels running. It is said once, at the lowest level it is true
    // at, which is the level worth aiming for.
    std::map<std::pair<int, string>, vector<string>> made;
    {
        std::set<std::pair<string, string>> said;      // (tier, noun)
        for (const ItemDef* r : items.Recipes()) {     // sorted by level: see ItemDatabase::Recipes
            if (CraftSkill(items.StationFor(*r)) != skill) continue;
            const ItemDef* result = items.Get(r->craft_result);
            if (!result) continue;
            const string noun = result->tier.empty() ? result->name : worn_noun(*result);
            if (!said.insert({result->tier, noun}).second) continue;
            note(made, r->craft_level, result->tier, noun);
        }
    }

    const auto emit = [&](const std::map<std::pair<int, string>, vector<string>>& from, const string& verb) {
        for (const auto& kv : from) {
            const TierDef* t = kv.first.second.empty() ? nullptr : items.Tier(kv.first.second);
            string text;
            if (t) {
                // Past three nouns it is the whole tier, and saying so is
                // shorter and truer than a list that runs off the column.
                text = t->name + " " + (kv.second.size() > 3 ? "gear" : join(kv.second, true));
            } else {
                // Odds and ends, which have nothing in common but a level:
                // name a few of them and count the rest.
                vector<string> few(kv.second.begin(), kv.second.begin() + std::min<size_t>(3, kv.second.size()));
                text = join(few, false);
                if (kv.second.size() > few.size())
                    text += " and " + std::to_string(kv.second.size() - few.size()) + " more";
            }
            add(kv.first.first, verb.empty() ? capitalised(text) : verb + " " + text);
        }
    };
    emit(gear, "");
    // Smiths smith, cooks cook: the same three stations under Crafting all
    // make, which is why the loom and the rack and the bench share a word.
    emit(made, skill == SKILL_SMITHING ? "Smith" : skill == SKILL_COOKING ? "Cook"
             : skill == SKILL_BREWING  ? "Brew"  : "Make");

    // --- the things one skill has and no other -------------------------------------
    if (skill == SKILL_MAGIC) {
        for (const auto& kv : spells.All()) {
            const SpellDef& s = kv.second;
            add(s.level, s.arcane ? s.name + " (ancient)" : s.name);
        }
        for (const EnchantDef* e : items.Enchantments())
            add(e->level, "Work " + e->name + " into a piece");
    }
    if (skill == SKILL_MINING)
        for (const TierDef& t : items.Tiers()) {
            const ItemDef* ore = t.ore.empty() ? nullptr : items.Get(t.ore);
            if (ore && t.mining > 1) add(t.mining, "Mine " + ore->name);
        }
    if (skill == SKILL_FISHING) {
        for (const auto& kv : items.All())
            if (kv.second.fish_level > 0) add(kv.second.fish_level, "Catch " + kv.second.name);
        for (const auto& m : Gathering::FishingMilestones()) {
            char buf[96];
            if (m.three > 0.0f) SDL_snprintf(buf, sizeof(buf), "Three fish in a cast, %d%% of the time",
                                             static_cast<int>(m.three * 100 + 0.5f));
            else                SDL_snprintf(buf, sizeof(buf), "Two fish in a cast, %d%% of the time",
                                             static_cast<int>(m.two * 100 + 0.5f));
            add(m.level, buf);
        }
    }
    if (skill == SKILL_FORAGING)
        for (const auto& kv : items.All())
            if (kv.second.forage_level > 0) add(kv.second.forage_level, "Pick " + kv.second.name);

    std::sort(out.begin(), out.end(), [](const SkillMilestone& a, const SkillMilestone& b) {
        if (a.level != b.level) return a.level < b.level;
        return a.text < b.text;
    });
    return out;
}

void Game::SyncMilestones() {
    if (milestones_for == cursor) return;
    milestones_for = cursor;
    milestones = MilestonesFor(cursor);
    // Open on the first one not yet reached: the page is for planning, and
    // what is already had is behind you.
    const int have = world->player.skills.Level(cursor);
    milestone_row = 0;
    for (size_t i = 0; i < milestones.size(); ++i)
        if (milestones[i].level > have) { milestone_row = static_cast<int>(i); break; }
}

void Game::UpdateSkillsPanel() {
    if (state_time <= 0.0f) {
        tree_reset_armed = false;
        // A fresh opening starts on the level list, and gathers again: a level
        // won since it was last open moves where the list should open.
        on_milestones = false;
        milestones_for = -1;
    }

    // I and O (the shoulder buttons on a pad) step between the level list and
    // the character's tree -- their path's, the only one they have; the panel
    // closes with Back.
    // ...and what the bosses have left them.
    const int tabs = TAB_COUNT;
    skills_tab = std::clamp(skills_tab, 0, tabs - 1);
    if (input.Pressed(Action::Inventory)) { skills_tab = (skills_tab + tabs - 1) % tabs; tree_reset_armed = false; on_milestones = false; Audio::Play(Sfx::UiMove); }
    if (input.Pressed(Action::Skills) || input.Pressed(Action::Ability)) {
        skills_tab = (skills_tab + 1) % tabs;
        tree_reset_armed = false;
        on_milestones = false;
        Audio::Play(Sfx::UiMove);
    }
    if (input.Pressed(Action::Back) || input.Pressed(Action::Pause)) {
        SetState(GameState::Play);
        return;
    }

    if (skills_tab == TAB_SKILLS) {
        SyncMilestones();
        // Left and right step between the two columns; up and down walk
        // whichever has the cursor. There is nothing to step into for a skill
        // that opens nothing, and Hitpoints is the one.
        if (milestones.empty()) on_milestones = false;
        else if (!on_milestones && input.MenuRight()) { on_milestones = true; Audio::Play(Sfx::UiMove); }
        else if (on_milestones && input.MenuLeft()) { on_milestones = false; Audio::Play(Sfx::UiMove); }
        if (on_milestones) {
            MoveCursor(milestone_row, static_cast<int>(milestones.size()), false);
        } else {
            MoveCursor(cursor, SKILL_COUNT);
            SyncMilestones();
        }
        return;
    }
    if (skills_tab == TAB_BOONS) return;         // the boons are read, not chosen
    if (skills_tab == TAB_BOOK) { UpdateSpellbook(); return; }

    Player& p = world->player;
    const AttackStyle style = p.talents.HasPath() ? p.talents.Path() : p.Affinity();
    const TalentTree& tree = skill_trees.Tree(style);

    // Stepping from the melee tree's fourth column to a tree with three.
    const int branches = tree.BranchCount();
    tree_branch = std::min(tree_branch, branches - 1);
    const int b0 = tree_branch, r0 = tree_row;
    if (input.MenuLeft())  tree_branch = (tree_branch + branches - 1) % branches;
    if (input.MenuRight()) tree_branch = (tree_branch + 1) % branches;
    if (input.MenuUp())    tree_row = std::max(0, tree_row - 1);
    if (input.MenuDown())  tree_row = std::min(SkillTrees::ROWS - 1, tree_row + 1);
    if (b0 != tree_branch || r0 != tree_row) Audio::Play(Sfx::UiMove);

    const TalentNode* node = tree.At(tree_branch, tree_row);
    if (node && input.Pressed(Action::Confirm)) {
        tree_reset_armed = false;
        const Talents::Why why = p.talents.CanLearn(node->id, p.skills);
        if (p.talents.Has(node->id) && !node->technique.empty()) {
            if (p.talents.ToggleTechnique(node->id)) {
                const bool on = p.talents.Technique(style) == node->technique;
                PushToast(on ? node->name + " is your charged attack now."
                             : "Back to a plain charged attack.", Palette::Xp);
                Audio::Play(Sfx::Equip);
            }
        } else if (p.talents.Has(node->id) && !node->ability.empty()) {
            // The first slot with room, then on down them, then put away.
            const int slot = p.talents.CycleAbility(node->id);
            const string keys = input.PromptFor(input.ShiftAction()) + " + " + input.PromptFor(AbilityButton(std::max(0, slot)));
            PushToast(slot >= 0 ? node->name + " is on " + keys + "." : node->name + " is put away.", Palette::Xp);
            Audio::Play(Sfx::Equip);
        } else {
            switch (why) {
                case Talents::Why::Ok: {
                    p.talents.Learn(node->id, p.skills);
                    const int rank = p.talents.Rank(node->id);
                    PushToast(node->ranks > 1 ? node->name + ", rank " + std::to_string(rank) + " of " + std::to_string(node->ranks) + "."
                                              : "Learned " + node->name + ".", Palette::Highlight);
                    Audio::Play(Sfx::QuestStart);
                    p.SyncMana();          // Deep Well is felt at once
                    // A first ability goes straight into a free slot, so it
                    // can be used without finding out how first.
                    if (!node->ability.empty() && rank == 1 && p.talents.SlotOf(node->id) < 0)
                        for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot)
                            if (!p.talents.Ability(slot)) {
                                while (p.talents.SlotOf(node->id) != slot) p.talents.CycleAbility(node->id);
                                PushToast(node->name + " is on " + input.PromptFor(input.ShiftAction()) + " + " +
                                          input.PromptFor(AbilityButton(slot)) + ".", Palette::Xp);
                                break;
                            }
                    break;
                }
                case Talents::Why::Learned:
                    PushToast(node->name + " has every rank it can.", Palette::TextDim);
                    break;
                case Talents::Why::Level:
                    PushToast("Needs " + string(SkillName(tree.skill)) + " " +
                              std::to_string(node->level) + ".", {235, 150, 120, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                case Talents::Why::Prerequisite:
                    PushToast("Learn the one above it first.", {235, 150, 120, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                case Talents::Why::NoPoints:
                    PushToast("No points left. One comes every " +
                              std::to_string(SkillTrees::LEVELS_PER_POINT) + " " +
                              SkillName(tree.skill) + " levels.", {235, 150, 120, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                default: break;
            }
        }
    }

    // Unlearning a tree takes two presses, so it cannot happen by accident.
    if (input.Pressed(Action::Target)) {
        // It costs: sixty coins for every point being taken back. Free and
        // unlimited, a build was whatever the next fight wanted -- thirty-three
        // points against forty-two ranks is only a choice if changing your
        // mind has a price. The first few points cost next to nothing to
        // rethink, and a finished tree costs about two thousand.
        const int spent = p.talents.PointsEarned(style, p.skills) - p.talents.PointsFree(style, p.skills);
        const int fee = Talents::RESPEC_FEE * std::max(0, spent);
        if (spent <= 0) {
            PushToast("There is nothing in the " + tree.name + " tree to unlearn.", Palette::TextDim);
        } else if (p.inventory.Coins() < fee) {
            tree_reset_armed = false;
            PushToast("Unlearning " + std::to_string(spent) + " points costs " + std::to_string(fee) +
                      " coins. You have " + std::to_string(p.inventory.Coins()) + ".", {235, 150, 120, 255});
            Audio::Play(Sfx::UiError);
        } else if (!tree_reset_armed) {
            tree_reset_armed = true;
            PushToast("Press " + input.PromptFor(Action::Target) + " again to unlearn the " +
                      tree.name + " tree for " + std::to_string(fee) + " coins.", {235, 190, 120, 255});
        } else {
            p.inventory.SpendCoins(fee);
            p.talents.Reset(style);
            tree_reset_armed = false;
            PushToast(tree.name + " tree unlearned for " + std::to_string(fee) + " coins. Its points are free again.",
                      Palette::TextDim);
            Audio::Play(Sfx::UiBack);
        }
    }
}

void Game::DrawSkillsPanel() {
    ui.Dim(0.5f);
    const Skills& s = world->player.skills;

    // A tree with a fourth branch -- melee has Footwork -- widens the panel by
    // a column, so the node descriptions keep the room they had.
    float tree_w = 860.0f;
    if (skills_tab == TAB_TREE) {
        const AttackStyle mine = world->player.talents.HasPath() ? world->player.talents.Path() : world->player.Affinity();
        tree_w += 172.0f * (skill_trees.Tree(mine).BranchCount() - SkillTrees::BRANCHES);
    }
    // As big as it wants to be, in a window with room for it; and in one
    // without, as big as the window. It was 640 and 690 tall whatever it was
    // drawn in, which is taller than a 1024 by 600 window and taller than a
    // Steam Deck's screen at the 125% its text wants -- the title and the
    // close prompt were both off the glass. (The overflow audit had a size for
    // exactly this and had never actually tested it: see Game::RunAudit.)
    const float fit_w = ui.ViewWidth() - 16.0f, fit_h = ui.ViewHeight() - 16.0f;
    // The level list carries a second column now -- what the selected skill
    // opens -- so the Skills page is as wide as the tree's, and every page
    // still gives way to a window too small to hold it.
    const SDL_FRect panel = CenteredPanel(ui, std::min(skills_tab == TAB_TREE ? tree_w : 1000.0f, fit_w),
                                          std::min(skills_tab == TAB_TREE ? 690.0f : 640.0f, fit_h));
    ui.Panel(panel);

    // --- tabs ------------------------------------------------------------------
    {
        // One tree a character: their path's. The hero's is the blade's, the
        // warden's the bow's, the wayfarer's the staff's.
        const AttackStyle path = world->player.talents.HasPath() ? world->player.talents.Path() : world->player.Affinity();
        const string tree_tab = skill_trees.Tree(path).name + " tree";
        const size_t boons = world->player.talents.Boons().size();
        // A mage's is a spellbook; for the other two the same page is mostly
        // about what they carry.
        const string kTabs[TAB_COUNT] = {"Skills", tree_tab, path == AttackStyle::Magic ? "Spellbook" : "Abilities",
                                         boons ? "Boons (" + std::to_string(boons) + ")" : string("Boons")};
        float tx = panel.x + 24.0f;
        for (int t = 0; t < TAB_COUNT; ++t) {
            const float w = ui.Measure(kTabs[t], TextSize::Body).x + 24.0f;
            const SDL_FRect tab = {tx, panel.y + 14.0f, w, 30.0f};
            const bool on = (t == skills_tab);
            ui.Fill(tab, on ? SDL_Color{70, 54, 30, 235} : SDL_Color{30, 24, 20, 200});
            ui.Outline(tab, on ? Palette::Highlight : Palette::BorderDim, on ? 2.0f : 1.0f);
            int free = 0;
            if (t == TAB_TREE) free = world->player.talents.PointsFree(path, s);
            ui.Text(kTabs[t], tab.x + 12.0f, tab.y + 5.0f, TextSize::Body,
                    on ? Palette::Highlight : (free > 0 ? Palette::Xp : Palette::Text));
            tx += w + 6.0f;
        }
        ui.Text(input.PromptFor(Action::Inventory) + " / " +
                    input.PromptFor(input.ActiveDevice() == InputMode::Controller ? Action::Ability : Action::Skills) + " switch",
                tx + 10.0f, panel.y + 22.0f, TextSize::Small, Palette::TextDim);
    }

    if (skills_tab == TAB_TREE) {
        DrawSkillTree(panel);
        return;
    }
    if (skills_tab == TAB_BOOK) {
        DrawSpellbook(panel);
        return;
    }
    if (skills_tab == TAB_BOONS) {
        DrawBoons(panel);
        return;
    }

    // The page is two columns: the levels on the left, and beside them what the
    // selected one is for. The milestone column takes about a third, and the
    // level rows lay themselves out in whatever is left rather than at fixed
    // offsets, so a small window narrows them instead of running them off.
    const float gap = 16.0f;
    const float mile_w = std::clamp(panel.w * 0.36f, 250.0f, 372.0f);
    const float list_w = panel.w - 40.0f - mile_w - gap;

    char header[128];
    SDL_snprintf(header, sizeof(header), "Combat %d    Total level %d    Total XP %lld",
                 s.CombatLevel(), s.TotalLevel(), s.TotalXp());
    ui.Text(header, panel.x + 20.0f + list_w, panel.y + panel.h - 52.0f, TextSize::Small,
            Palette::TextDim, Align::Right);

    // Fourteen rows in whatever is left between the tabs and the footer.
    const float row_h = std::clamp(floorf((panel.h - 58.0f - 134.0f) / static_cast<float>(SKILL_COUNT)), 24.0f, 32.0f);
    for (int i = 0; i < SKILL_COUNT; ++i) {
        const SDL_FRect row = {panel.x + 20.0f, panel.y + 58.0f + i * row_h,
                               list_w, row_h - 4.0f};
        const bool selected = (i == cursor);
        if (selected) {
            ui.Fill(row, {58, 46, 28, 200});
            ui.Outline(row, on_milestones ? Palette::BorderDim : Palette::Highlight, 1.0f);
        }

        const int level = s.Level(i);
        ui.Text(SkillName(i), row.x + 10.0f, row.y + 4.0f, TextSize::Body,
                selected ? Palette::Highlight : Palette::Text);
        // A boosted level shows what it is working at right now.
        const int now = s.Current(i);
        const bool boosted = i != SKILL_HITPOINTS && now != level;
        const float lvl_x = std::max(110.0f, row.w * 0.30f);
        const float tail = std::max(92.0f, row.w * 0.22f);
        ui.Text(boosted ? std::to_string(now) + "/" + std::to_string(level) : std::to_string(level),
                row.x + lvl_x, row.y + 4.0f, TextSize::Body,
                boosted ? (now > level ? Palette::Xp : SDL_Color{235, 150, 120, 255}) : Palette::Text, Align::Right);

        // Progress toward the next level, the way the OSRS skill guide reads.
        const int xp = s.Xp(i);
        const int here = XpForLevel(level);
        const int next = XpForLevel(std::min(level + 1, MAX_SKILL_LEVEL));
        const float frac = (next > here) ? static_cast<float>(xp - here) / (next - here) : 1.0f;

        const SDL_FRect bar = {row.x + lvl_x + 16.0f, row.y + 8.0f,
                               std::max(24.0f, row.w - lvl_x - 16.0f - tail), 14.0f};
        ui.Bar(bar, frac, Palette::Xp, {26, 34, 26, 235});

        char xp_text[48];
        if (level >= MAX_SKILL_LEVEL) SDL_snprintf(xp_text, sizeof(xp_text), "max");
        else SDL_snprintf(xp_text, sizeof(xp_text), "%d xp to %d", next - xp, level + 1);
        ui.Text(xp_text, row.x + row.w - 10.0f, row.y + 7.0f, TextSize::Small,
                Palette::TextDim, Align::Right);
    }

    DrawMilestones({panel.x + 20.0f + list_w + gap, panel.y + 58.0f, mile_w, panel.h - 116.0f});

    ui.Text(input.PromptFor(Action::Back) + " close", panel.x + panel.w / 2.0f,
            panel.y + panel.h - 28.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

// Everything the selected skill opens, in the order it opens it, with the
// experience still owed on whatever the cursor is standing on. Walking it is
// how a level gets aimed at: see Game::MilestonesFor.
void Game::DrawMilestones(const SDL_FRect& col) {
    // Gathered here as well as in the update, so a frame drawn without one --
    // which is every pass of the layout audit -- still has its list.
    SyncMilestones();
    const Skills& s = world->player.skills;
    const int have = s.Level(cursor);
    ui.Fill(col, {22, 19, 16, 170});
    ui.Outline(col, on_milestones ? Palette::Highlight : Palette::BorderDim, on_milestones ? 2.0f : 1.0f);

    // A line trimmed to the width it has, because a long node name in a narrow
    // window is a line running off a panel: see UI::BeginAudit.
    const auto fit = [&](string t, float w) {
        if (ui.Measure(t, TextSize::Small).x <= w) return t;
        while (t.size() > 2 && ui.Measure(t + "...", TextSize::Small).x > w) t.pop_back();
        while (!t.empty() && t.back() == ' ') t.pop_back();
        return t + "...";
    };

    ui.Text(string(SkillName(cursor)) + " opens", col.x + 10.0f, col.y + 7.0f, TextSize::Body, Palette::Highlight);
    const float top = col.y + 32.0f;
    const float foot = 34.0f;                 // the line the cursor's own row explains
    const float row_h = 21.0f;
    const int shown = std::max(1, static_cast<int>((col.h - 32.0f - foot) / row_h));

    if (milestones.empty()) {
        // Strength and Hitpoints: nothing is gated behind either. They are the
        // two that pay at every level rather than at a few of them.
        ui.TextWrapped("Nothing in the realm opens at a level of it. What it gives, it gives all the way up.",
                       col.x + 10.0f, top + 4.0f, col.w - 20.0f, TextSize::Small, Palette::TextDim);
        return;
    }

    // Scrolled so the cursor's row is on the page, and no further: the list
    // does not jump about while it is walked.
    const int count = static_cast<int>(milestones.size());
    milestone_row = std::clamp(milestone_row, 0, count - 1);
    int first = std::clamp(milestone_row - shown / 2, 0, std::max(0, count - shown));
    for (int i = 0; i < shown && first + i < count; ++i) {
        const SkillMilestone& m = milestones[first + i];
        const bool here = (first + i == milestone_row);
        const bool reached = m.level <= have;
        const SDL_FRect row = {col.x + 4.0f, top + i * row_h, col.w - 8.0f, row_h - 2.0f};
        if (here) {
            ui.Fill(row, on_milestones ? SDL_Color{58, 46, 28, 210} : SDL_Color{38, 32, 24, 170});
            if (on_milestones) ui.Outline(row, Palette::Highlight, 1.0f);
        }
        const SDL_Color c = here ? Palette::Highlight : (reached ? Palette::Text : Palette::TextDim);
        ui.Text(std::to_string(m.level), row.x + 28.0f, row.y + 2.0f, TextSize::Small,
                reached ? Palette::Xp : c, Align::Right);
        ui.Text(fit(m.text, row.w - 42.0f), row.x + 36.0f, row.y + 2.0f, TextSize::Small, c);
    }

    // What the cursor is standing on costs this much more: the whole point of
    // the page. Experience owed, not levels, because experience is what the
    // next hour of play actually pays in.
    const SkillMilestone& at = milestones[milestone_row];
    char note[96];
    if (at.level <= have) SDL_snprintf(note, sizeof(note), "%s %d -- reached", SkillName(cursor), at.level);
    else SDL_snprintf(note, sizeof(note), "%s %d -- %d xp to go", SkillName(cursor), at.level,
                      XpForLevel(at.level) - s.Xp(cursor));
    ui.Text(fit(note, col.w - 20.0f), col.x + 10.0f, col.y + col.h - foot + 6.0f, TextSize::Small,
            at.level <= have ? Palette::Xp : Palette::Text);
    // Menu movement is the movement keys: see Input::MenuLeft.
    ui.Text(fit(on_milestones ? input.PromptFor(Action::MoveLeft) + " back to the skills"
                              : input.PromptFor(Action::MoveRight) + " walk them", col.w - 20.0f),
            col.x + 10.0f, col.y + col.h - foot + 20.0f, TextSize::Small, Palette::TextDim);
}

// -----------------------------------------------------------------------------
//  The spellbook
//
//  What each button does was spread over three places, and one of them was
//  nowhere: an ability was moved from slot to slot by pressing confirm on it in
//  the tree until it came round; an ancient spell was chosen by pressing 5 until
//  it came round, which a pad could not do at all; and which of an element's
//  spells was cast was not a choice -- it was the strongest, at the strongest's
//  price. This is the one page for all of it: a row for every slot, and on each
//  row everything this character has that could go in it.
// -----------------------------------------------------------------------------

vector<Game::BookRow> Game::SpellbookRows() const {
    const Player& p = world->player;
    const int magic = p.skills.Level(SKILL_MAGIC);
    const AttackStyle path = p.talents.HasPath() ? p.talents.Path() : p.Affinity();
    const TalentTree& tree = skill_trees.Tree(path);
    vector<BookRow> spell_rows, ability_rows;

    const auto spell_note = [](const SpellDef& s) {
        char buf[64];
        SDL_snprintf(buf, sizeof(buf), "%d mana   %.1fx", s.mana, s.damage_mult);
        return string(buf);
    };

    // --- an element's own staff: its four spells, as they are ------------------------------
    const Element staff = p.StaffElement();
    for (int i = 1; i < 4 && staff != Element::None; ++i) {
        BookRow row;
        row.kind = BookRow::Kind::Spell;
        row.element = staff;
        row.slot = i;
        string name = ElementName(staff);
        name[0] = static_cast<char>(toupper(static_cast<unsigned char>(name[0])));
        row.label = name;
        row.color = ElementColor(staff);
        const SpellDef* have = spells.ForSlot(staff, i + 1, magic);
        const SpellDef* first = spells.FirstOnSlot(staff, i + 1);
        if (have) {
            BookOption o;
            o.id = have->id; o.name = have->name; o.note = spell_note(*have);
            o.text = have->description + " It is this staff's: no other casts it.";
            row.options.push_back(o);
        } else {
            row.nothing = first ? "Magic " + std::to_string(first->level) + " for " + first->name : string("Nothing known");
        }
        spell_rows.push_back(row);
    }

    // --- the four elements: the strongest, or held to one of the lesser ------------------
    static const Element kElements[4] = {Element::Fire, Element::Water, Element::Earth, Element::Air};
    for (int i = 0; i < 4; ++i) {
        // With an element's staff in hand there is one element, and it is the first row.
        if (staff != Element::None && kElements[i] != staff) continue;
        BookRow row;
        row.kind = BookRow::Kind::Spell;
        row.element = kElements[i];
        row.slot = staff != Element::None ? 0 : i;
        string name = ElementName(kElements[i]);
        name[0] = static_cast<char>(toupper(static_cast<unsigned char>(name[0])));
        row.label = name;
        row.color = ElementColor(kElements[i]);
        const SpellDef* best = spells.BestFor(kElements[i], magic);
        const SpellDef* next = spells.NextFor(kElements[i], magic);
        if (!best) {
            row.nothing = next ? "Magic " + std::to_string(next->level) + " for " + next->name : string("Nothing known");
        } else {
            BookOption strongest;
            strongest.name = "Strongest: " + best->name;
            strongest.note = spell_note(*best);
            strongest.text = best->description + " Always the strongest your Magic can cast" +
                             (next ? ": " + next->name + " at Magic " + std::to_string(next->level) + "." : string("."));
            row.options.push_back(strongest);
            // What this weapon reaches of the element, which is not the same
            // for a staff, a wand, a grimoire and an orb.
            const vector<int>& slots = p.SpellSlots(kElements[i]);
            vector<const SpellDef*> offered = slots.empty() ? spells.Of(kElements[i])
                                                            : spells.ForWeapon(kElements[i], slots, magic);
            for (const SpellDef* s : offered) {
                if (s->level > magic) continue;
                BookOption o;
                o.id = s->id;
                o.name = s->name;
                o.note = spell_note(*s);
                o.text = s->description + (s == best ? " Held to this one, whatever your Magic comes to."
                                                     : " Held to this one: weaker than " + best->name + ", and cheaper.");
                row.options.push_back(o);
                if (p.HeldSpell(kElements[i]) == s->id) row.chosen = static_cast<int>(row.options.size()) - 1;
            }
        }
        if (staff != Element::None) spell_rows.insert(spell_rows.begin(), row);
        else                        spell_rows.push_back(row);
    }

    // --- the lightning: which of the five is on the key -----------------------------------
    // It is chosen the way the ancient magic is, and for the same reason: five
    // spells, and no staff of its own to put them on the number row. What it
    // costs of the battery is part of what a choice is, so the note says it.
    {
        BookRow row;
        row.kind = BookRow::Kind::Lightning;
        row.element = Element::Electric;
        row.slot = 4;
        row.label = "Lightning";
        row.color = ElementColor(Element::Electric);
        for (const SpellDef* s : spells.Electric(MAX_SKILL_LEVEL)) {
            BookOption o;
            o.id = s->id;
            o.name = s->name;
            o.usable = s->level <= magic;
            o.note = o.usable ? spell_note(*s) : "needs Magic " + std::to_string(s->level);
            if (o.usable) {
                const int cost = static_cast<int>(std::lround(s->battery_cost * 100.0f));
                const int gain = static_cast<int>(std::lround(s->battery_gain * 100.0f));
                if (s->battery_cost >= 1.0f) o.note += "   the whole charge";
                else if (cost > 0)           o.note += "   " + std::to_string(cost) + "% charge";
                if (gain > 0)                o.note += "   +" + std::to_string(gain) + "% charge";
            }
            o.text = s->description;
            row.options.push_back(o);
            if (p.ElectricSpell() == s->id) row.chosen = static_cast<int>(row.options.size()) - 1;
        }
        if (row.options.empty()) row.nothing = "Nothing known. Magic 12 for Zap, the first of it.";
        spell_rows.push_back(row);
    }

    // --- the sixth: which of the ancient spells is on it ---------------------------------
    {
        BookRow row;
        row.kind = BookRow::Kind::Ancient;
        row.element = Element::Arcane;
        row.slot = 5;
        row.label = "Ancient";
        row.color = ElementColor(Element::Arcane);
        for (const string& id : world->KnownArcane(spells)) {
            const SpellDef* s = spells.Get(id);
            if (!s) continue;
            BookOption o;
            o.id = id;
            o.name = s->name;
            o.usable = s->level <= magic;
            o.note = o.usable ? spell_note(*s) : "needs Magic " + std::to_string(s->level);
            o.text = s->description;
            row.options.push_back(o);
            if (p.ArcaneSpell() == id) row.chosen = static_cast<int>(row.options.size()) - 1;
        }
        if (row.options.empty()) row.nothing = "None known. The college at Fernhollow teaches it.";
        spell_rows.push_back(row);
    }

    // --- the three abilities carried ---------------------------------------------------
    for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot) {
        BookRow row;
        row.kind = BookRow::Kind::Ability;
        row.slot = slot;
        row.label = input.PromptFor(input.ShiftAction()) + " + " + input.PromptFor(AbilityButton(slot));
        row.color = {130, 190, 240, 255};
        BookOption none;
        none.name = "Nothing";
        none.text = "Nothing is carried here.";
        row.options.push_back(none);
        for (const TalentNode& n : tree.nodes) {
            if (n.ability.empty() || !p.talents.Has(n.id)) continue;
            BookOption o;
            o.id = n.id;
            o.name = n.name;
            o.note = std::to_string(static_cast<int>(n.cooldown)) + " s";
            if (n.stamina_cost > 0) o.note += "   " + std::to_string(n.stamina_cost) + " stamina";
            if (n.mana_cost > 0)    o.note += "   " + std::to_string(n.mana_cost) + " mana";
            o.text = n.description;
            const int at = p.talents.SlotOf(n.id);
            if (at >= 0 && at != slot) o.text += " It is in slot " + std::to_string(at + 1) + " now: the two will change places.";
            row.options.push_back(o);
            if (at == slot) row.chosen = static_cast<int>(row.options.size()) - 1;
        }
        if (row.options.size() == 1) {
            row.options.clear();
            row.nothing = "No abilities learned. They are in the " + tree.name + " tree.";
        }
        ability_rows.push_back(row);
    }

    // --- and what a held heavy attack comes out as ---------------------------------------
    {
        BookRow row;
        row.kind = BookRow::Kind::Technique;
        row.label = "Hold " + input.PromptFor(Action::StrongAttack);
        row.color = {236, 150, 110, 255};
        BookOption plain;
        plain.name = "Plain charged attack";
        plain.text = "The charged attack as it comes, with nothing from the tree on it.";
        row.options.push_back(plain);
        for (const TalentNode& n : tree.nodes) {
            if (n.technique.empty() || !p.talents.Has(n.id)) continue;
            BookOption o;
            o.id = n.id;
            o.name = n.name;
            o.text = n.description;
            row.options.push_back(o);
            if (p.talents.Technique(path) == n.technique) row.chosen = static_cast<int>(row.options.size()) - 1;
        }
        if (row.options.size() == 1) {
            row.options.clear();
            row.nothing = "No techniques learned. They are in the " + tree.name + " tree.";
        }
        ability_rows.push_back(row);
    }

    // A mage's spells come first and anyone else's abilities do: the page is
    // headed by what the character is for. Everyone has both -- a staff is a
    // staff in anybody's hands.
    vector<BookRow> rows = path == AttackStyle::Magic ? spell_rows : ability_rows;
    const vector<BookRow>& rest = path == AttackStyle::Magic ? ability_rows : spell_rows;
    rows.insert(rows.end(), rest.begin(), rest.end());
    return rows;
}

void Game::ChooseInBook(const BookRow& row, int option) {
    if (option < 0 || option >= static_cast<int>(row.options.size())) return;
    Player& p = world->player;
    const BookOption& o = row.options[option];
    switch (row.kind) {
        // An element's own staff has a row for each of its other spells, and
        // there is nothing to choose on them.
        case BookRow::Kind::Spell:     if (!(p.StaffElement() != Element::None && row.slot > 0)) p.HoldSpell(row.element, o.id); break;
        case BookRow::Kind::Ancient:   p.SetArcaneSpell(o.id); break;
        case BookRow::Kind::Lightning: p.SetElectricSpell(o.id); break;
        case BookRow::Kind::Ability:   p.talents.SetAbility(row.slot, o.id); break;
        case BookRow::Kind::Technique:
            p.talents.SetTechnique(p.talents.HasPath() ? p.talents.Path() : p.Affinity(), o.id);
            break;
    }
}

vector<const SpellDef*> Game::SpellsInSlot() const {
    return world->player.SpellChoices(spells, world->KnownArcane(spells), KnownElectric());
}

void Game::StepSpell(int step) {
    Player& p = world->player;
    if (p.Style() != AttackStyle::Magic) return;
    const vector<string> arcane = world->KnownArcane(spells), electric = KnownElectric();
    if (!p.StepSpell(step, spells, arcane, electric)) {
        const vector<const SpellDef*> list = p.SpellChoices(spells, arcane, electric);
        PushToast(list.empty() ? string("Nothing to cast here yet.")
                               : "Only " + list.front()->name + " here, for now.", Palette::TextDim);
        Audio::Play(Sfx::UiError);
        return;
    }
    spell_flash = 0.6f;
    Audio::Play(Sfx::UiMove);
}

void Game::UpdateSpellbook() {
    const vector<BookRow> rows = SpellbookRows();
    const int count = static_cast<int>(rows.size());
    book_row = std::clamp(book_row, 0, std::max(0, count - 1));
    const int was = book_row;
    if (input.MenuUp())   book_row = (book_row + count - 1) % count;
    if (input.MenuDown()) book_row = (book_row + 1) % count;
    if (was != book_row) { Audio::Play(Sfx::UiMove); return; }

    const BookRow& row = rows[book_row];
    const int options = static_cast<int>(row.options.size());
    int step = 0;
    if (input.MenuLeft())  step = -1;
    if (input.MenuRight() || input.Pressed(Action::Confirm)) step = 1;
    if (step == 0) return;
    if (options < 2) {
        if (!row.nothing.empty()) PushToast(row.nothing, Palette::TextDim);
        Audio::Play(Sfx::UiError);
        return;
    }
    ChooseInBook(row, (row.chosen + step + options) % options);
    Audio::Play(Sfx::Equip);
}

void Game::DrawSpellbook(const SDL_FRect& panel) {
    const Player& p = world->player;
    const vector<BookRow> rows = SpellbookRows();
    const float x = panel.x + 24.0f, w = panel.w - 48.0f;
    float y = panel.y + 56.0f;
    y += ui.TextWrapped("What your buttons do. Up and down choose a slot; left and right change what is in it, "
                        "from everything you have learned.",
                        x, y, w, TextSize::Small, Palette::TextDim) + 8.0f;

    // Nine rows and two headings, in what is left above the description: the
    // rows give up height in a short window before anything is cut off.
    const float foot = 150.0f;
    const float room = panel.y + panel.h - foot - y;
    const float head_h = 24.0f;
    const float row_h = std::clamp(floorf((room - head_h * 2.0f) / std::max<size_t>(1, rows.size())), 24.0f, 36.0f);

    const auto heading = [&](const string& text) {
        ui.Text(text, x, y + 3.0f, TextSize::Small, Palette::Highlight);
        y += head_h;
    };
    const bool staff = p.Style() == AttackStyle::Magic;
    bool spells_headed = false, abilities_headed = false;

    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const BookRow& row = rows[i];
        const bool spell = row.kind == BookRow::Kind::Spell || row.kind == BookRow::Kind::Ancient ||
                           row.kind == BookRow::Kind::Lightning;
        if (spell && !spells_headed) {
            spells_headed = true;
            heading(staff ? "Spells" : "Spells, with a staff in hand");
        }
        if (!spell && !abilities_headed) {
            abilities_headed = true;
            heading("Abilities and the charged attack");
        }

        const SDL_FRect box = {x, y, w, row_h - 4.0f};
        const bool on = i == book_row;
        ui.Fill(box, on ? SDL_Color{58, 46, 28, 220} : SDL_Color{30, 24, 20, 190});
        ui.Outline(box, on ? Palette::Highlight : Palette::BorderDim, on ? 2.0f : 1.0f);
        const float ty = box.y + (box.h - 18.0f) / 2.0f;

        // Which button, in the slot's own colour; a spell slot leads with its number.
        const string label = spell ? std::to_string(row.slot + 1) + "   " + row.label : row.label;
        ui.Text(label, box.x + 10.0f, ty, TextSize::Small, row.color);

        const float cx = box.x + 150.0f;
        if (row.options.empty()) {
            ui.Text(row.nothing, cx, ty, TextSize::Small, Palette::TextDim);
        } else {
            const BookOption& o = row.options[std::clamp(row.chosen, 0, static_cast<int>(row.options.size()) - 1)];
            const bool more = row.options.size() > 1;
            const string shown = (on && more ? "<  " : "") + o.name + (on && more ? "  >" : "");
            ui.Text(shown, cx, ty, TextSize::Small,
                    !o.usable ? SDL_Color{225, 130, 120, 255} : on ? Palette::Highlight : Palette::Text);
            ui.Text(o.note, box.x + box.w - 10.0f, ty, TextSize::Small,
                    o.usable ? Palette::TextDim : SDL_Color{225, 130, 120, 255}, Align::Right);
        }
        y += row_h;
    }

    // --- what is on the chosen row ---------------------------------------------------------
    const float dy = panel.y + panel.h - foot + 8.0f;
    ui.Fill({x, dy, w, 1.0f}, Palette::BorderDim);
    if (book_row >= 0 && book_row < static_cast<int>(rows.size())) {
        const BookRow& row = rows[book_row];
        float ly = dy + 10.0f;
        if (row.options.empty()) {
            ui.TextWrapped(row.nothing, x, ly, w, TextSize::Small, Palette::TextDim);
        } else {
            const BookOption& o = row.options[std::clamp(row.chosen, 0, static_cast<int>(row.options.size()) - 1)];
            ui.Text(o.name, x, ly, TextSize::Body, o.usable ? Palette::Highlight : SDL_Color{225, 130, 120, 255});
            ui.Text(std::to_string(row.chosen + 1) + " of " + std::to_string(row.options.size()),
                    x + w, ly + 3.0f, TextSize::Small, Palette::TextDim, Align::Right);
            ly += 26.0f;
            ui.TextWrapped(o.text, x, ly, w, TextSize::Small, Palette::Text);
        }
    }

    ui.Text(input.PromptFor(Action::Back) + " close", panel.x + panel.w / 2.0f,
            panel.y + panel.h - 28.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

// What the bosses have left this character: see Talents::SlayBoss. Nothing to
// choose here -- a boon is given, not bought -- so it is a page to read.
void Game::DrawBoons(const SDL_FRect& panel) {
    const Player& p = world->player;
    const float x = panel.x + 24.0f, w = panel.w - 48.0f;
    float y = panel.y + 58.0f;
    y += ui.TextWrapped("The first time you bring down one of the great ones it leaves you two things, for good: "
                        "a skill point for your tree, and a boon. The fifteenth time, its totem, for the ring in your house.",
                        x, y, w, TextSize::Small, Palette::TextDim) + 14.0f;

    const vector<string>& mine = p.talents.Boons();
    if (mine.empty()) {
        ui.Text("Nothing yet.", panel.x + panel.w / 2.0f, panel.y + panel.h / 2.0f - 30.0f, TextSize::Body,
                Palette::TextDim, Align::Center);
        ui.TextWrapped("There is one under the Barley and Bell, if Bess has not mentioned it.",
                       x + 60.0f, panel.y + panel.h / 2.0f, w - 120.0f, TextSize::Small, Palette::TextDim);
    }
    // As many rows as there are bosses in the game would need, in the room there is.
    const float room = panel.y + panel.h - 112.0f - y;
    const float row_h = std::clamp(floorf(room / std::max<size_t>(1, mine.size())), 26.0f, 40.0f);
    for (const string& id : mine) {
        const BoonDef* b = skill_trees.Boon(id);
        if (!b || y + row_h > panel.y + panel.h - 108.0f) continue;
        const SDL_FRect row = {x, y, w, row_h - 4.0f};
        ui.Fill(row, {40, 32, 22, 220});
        ui.Outline(row, {232, 190, 96, 255}, 1.0f);
        const float ty = row.y + (row.h - 20.0f) / 2.0f;
        ui.Text(b->name, row.x + 12.0f, ty, TextSize::Body, Palette::Highlight);
        ui.Text(b->text, row.x + row.w - 12.0f, ty + 3.0f, TextSize::Small, Palette::Text, Align::Right);
        y += row_h;
    }

    // Who has been killed, by name, so that it is plain which are still to do.
    // And how many times, because the fifteenth is the one that leaves a totem.
    string fallen;
    for (const string& id : p.talents.BossesSlain()) {
        const EnemyDef* d = enemy_db.Get(id);
        const int n = p.talents.Kills(id);
        fallen += (fallen.empty() ? "" : ", ") + (d ? d->name : id) + " " +
                  (n >= Talents::TOTEM_KILLS ? string("(totem)") : std::to_string(n) + "/" + std::to_string(Talents::TOTEM_KILLS));
    }
    if (!fallen.empty())
        ui.TextWrapped("Brought down: " + fallen + ".", x, panel.y + panel.h - 104.0f, w, TextSize::Small, Palette::TextDim);

    ui.Text(input.PromptFor(Action::Back) + " close", panel.x + panel.w / 2.0f,
            panel.y + panel.h - 28.0f, TextSize::Small, Palette::TextDim, Align::Center);
}

void Game::DrawSkillTree(const SDL_FRect& panel) {
    const Player& p = world->player;
    const AttackStyle style = p.talents.HasPath() ? p.talents.Path() : p.Affinity();
    const TalentTree& tree = skill_trees.Tree(style);
    const int level = p.skills.Level(tree.skill);
    const int earned = p.talents.PointsEarned(style, p.skills);
    const int free = p.talents.PointsFree(style, p.skills);

    char head[200];
    const int from_bosses = p.talents.BonusPoints();
    if (from_bosses > 0)
        SDL_snprintf(head, sizeof(head), "%s %d     %d of %d points free     a point every %d levels, and %d from bosses",
                     SkillName(tree.skill), level, free, earned, SkillTrees::LEVELS_PER_POINT, from_bosses);
    else
        SDL_snprintf(head, sizeof(head), "%s %d     %d of %d points free     a point every %d levels, and one for each boss",
                     SkillName(tree.skill), level, free, earned, SkillTrees::LEVELS_PER_POINT);
    ui.Text(head, panel.x + 24.0f, panel.y + 56.0f, TextSize::Small, free > 0 ? Palette::Xp : Palette::TextDim);

    // --- the grid ----------------------------------------------------------------
    const float gx = panel.x + 78.0f, gy = panel.y + 112.0f;
    // The grid is cut to the panel, which is cut to the window: the melee
    // tree's four columns are 1032 wide at their full size, and eight rows of
    // 62 are 496 before the headings. Columns give up width and rows give up
    // the gap between boxes before either gives up the box itself, which has
    // two lines of text to hold.
    const int   columns = std::max(1, tree.BranchCount());
    const float col_w = std::min(172.0f, floorf((panel.w - 78.0f - 36.0f - 230.0f) / columns));
    const float row_h = std::clamp(floorf((panel.h - 112.0f - 82.0f) / SkillTrees::ROWS), 46.0f, 62.0f);
    const float box_w = col_w - 22.0f, box_h = std::min(44.0f, row_h - 6.0f);

    for (int b = 0; b < tree.BranchCount(); ++b) {
        const string name = b < static_cast<int>(tree.branches.size()) ? tree.branches[b] : "";
        ui.Text(name, gx + b * col_w + box_w / 2.0f, gy - 26.0f, TextSize::Body, Palette::Highlight, Align::Center);
    }

    for (int row = 0; row < SkillTrees::ROWS; ++row) {
        const TalentNode* first = tree.At(0, row);
        if (first)
            ui.Text("Lv " + std::to_string(first->level), panel.x + 24.0f, gy + row * row_h + 13.0f,
                    TextSize::Small, level >= first->level ? Palette::Text : Palette::TextDim);
    }

    for (const TalentNode& n : tree.nodes) {
        const SDL_FRect box = {gx + n.branch * col_w, gy + n.row * row_h, box_w, box_h};
        const bool learned = p.talents.Has(n.id);
        const int  rank = p.talents.Rank(n.id);
        const Talents::Why why = p.talents.CanLearn(n.id, p.skills);
        const bool available = why == Talents::Why::Ok;
        const bool selected = n.branch == tree_branch && n.row == tree_row;
        const int  slot = p.talents.SlotOf(n.id);
        const bool active = (!n.technique.empty() && p.talents.Technique(style) == n.technique) || slot >= 0;

        // The line down to the next node in the branch, lit once both ends are.
        if (const TalentNode* below = n.row + 1 < SkillTrees::ROWS ? tree.At(n.branch, n.row + 1) : nullptr) {
            const bool lit = learned && p.talents.Has(below->id);
            ui.Fill({box.x + box_w / 2.0f - 1.0f, box.y + box_h, 3.0f, row_h - box_h},
                    lit ? SDL_Color{232, 190, 96, 255} : SDL_Color{70, 60, 50, 255});
        }

        SDL_Color fill = {26, 21, 18, 235}, text = {120, 110, 100, 255}, edge = Palette::BorderDim;
        if (learned)        { fill = {92, 70, 30, 240}; text = Palette::Highlight; edge = {232, 190, 96, 255}; }
        else if (available) { fill = {40, 50, 30, 240}; text = Palette::Text; edge = Palette::Xp; }
        if (active)         { fill = {120, 50, 36, 245}; edge = {255, 160, 110, 255}; }
        ui.Fill(box, fill);
        ui.Outline(box, selected ? SDL_Color{255, 255, 255, 255} : edge, selected ? 3.0f : 1.0f);

        ui.Text(n.name, box.x + box_w / 2.0f, box.y + (box_h >= 44.0f ? 5.0f : 3.0f), TextSize::Small, text, Align::Center);
        // Rushing Strike is neither a charged technique nor a passive: it is a
        // move of its own, made on its own button.
        string kind = !n.technique.empty() ? string(active ? "technique - active" : "technique")
                    : !n.ability.empty() ? (slot >= 0 ? "ability - slot " + std::to_string(slot + 1) : string("ability"))
                    : n.effects.count("rushing_strike") ? string("move")
                    : n.row == SkillTrees::ROWS - 1 ? string("capstone") : string("passive");
        if (n.ranks > 1) kind += "  " + std::to_string(rank) + "/" + std::to_string(n.ranks);
        ui.Text(kind, box.x + box_w / 2.0f, box.y + box_h - 21.0f, TextSize::Small,
                !n.technique.empty() ? SDL_Color{236, 150, 110, 255}
                : !n.ability.empty() ? SDL_Color{130, 190, 240, 255} : Palette::TextDim, Align::Center);
    }

    // --- the chosen node -------------------------------------------------------------
    const TalentNode* n = tree.At(tree_branch, tree_row);
    const float dx = gx + tree.BranchCount() * col_w + 12.0f;
    const float dw = panel.x + panel.w - dx - 24.0f;
    float y = gy - 26.0f;
    if (n) {
        ui.Text(n->name, dx, y, TextSize::Body, Palette::Highlight);
        y += 28.0f;
        const string each = n->ranks > 1 ? "a point a rank" : "one point";
        // Wrapped, not written straight out: "Attack 47, a point a rank, after
        // War Cry" is wider than the column the rest of this is written in, and
        // ran off the side of the panel.
        y += ui.TextWrapped(string(SkillName(tree.skill)) + " " + std::to_string(n->level) + ", " +
                            (n->row == 0 || !tree.At(n->branch, n->row - 1)
                                 ? each
                                 : each + ", after " + tree.At(n->branch, n->row - 1)->name),
                            dx, y, dw, TextSize::Small,
                            level >= n->level ? Palette::TextDim : SDL_Color{225, 130, 120, 255}) + 6.0f;
        y += ui.TextWrapped(n->description, dx, y, dw, TextSize::Small, Palette::Text) + 10.0f;
        if (!n->ability.empty()) {
            string cost = "Every " + std::to_string(static_cast<int>(n->cooldown)) + " seconds";
            if (n->stamina_cost > 0) cost += ", " + std::to_string(n->stamina_cost) + " stamina";
            if (n->mana_cost > 0)    cost += ", " + std::to_string(n->mana_cost) + " mana";
            y += ui.TextWrapped(cost + ". Hold " + input.PromptFor(Action::Block) + " and press " +
                                input.PromptFor(Action::LightAttack) + ", " + input.PromptFor(Action::StrongAttack) +
                                " or " + input.PromptFor(Action::Target) + ", whichever slot it is in. Three are carried at once.",
                                dx, y, dw, TextSize::Small, {130, 190, 240, 255}) + 10.0f;
        }

        string status, action;
        const Talents::Why why = p.talents.CanLearn(n->id, p.skills);
        const int rank = p.talents.Rank(n->id);
        if (p.talents.Has(n->id) && !n->technique.empty()) {
            const bool active = p.talents.Technique(style) == n->technique;
            status = active ? "Your charged attack with this style." : "Learned, not in use.";
            action = input.PromptFor(Action::Confirm) + (active ? " stop using it" : " use as charged attack");
        } else if (p.talents.Has(n->id) && !n->ability.empty()) {
            const int slot = p.talents.SlotOf(n->id);
            status = slot >= 0 ? "Carried in slot " + std::to_string(slot + 1) + ": " + input.PromptFor(input.ShiftAction()) + " + " +
                                 input.PromptFor(AbilityButton(slot)) + "."
                               : "Learned, not carried.";
            action = input.PromptFor(Action::Confirm) +
                     (slot < 0 ? string(" carry it")
                      : slot + 1 < SkillTrees::ABILITY_SLOTS ? " move to slot " + std::to_string(slot + 2) : string(" put it away"));
        } else if (p.talents.Has(n->id) && why != Talents::Why::Ok) {
            status = n->ranks > 1 ? "Rank " + std::to_string(rank) + " of " + std::to_string(n->ranks) + "." : "Learned.";
            if (rank < n->ranks && why == Talents::Why::NoPoints) status += " No points for the next.";
        } else if (why == Talents::Why::Ok && rank > 0) {
            status = "Rank " + std::to_string(rank) + " of " + std::to_string(n->ranks) + ".";
            action = input.PromptFor(Action::Confirm) + " learn the next rank";
        } else if (why == Talents::Why::Ok) {
            status = "Ready to learn.";
            action = input.PromptFor(Action::Confirm) + " learn";
        } else if (why == Talents::Why::Level) {
            status = "Needs " + string(SkillName(tree.skill)) + " " + std::to_string(n->level) + ".";
        } else if (why == Talents::Why::Prerequisite) {
            status = "Learn the node above it first.";
        } else if (why == Talents::Why::NoPoints) {
            status = "No points to spend.";
        }
        y += ui.TextWrapped(status, dx, y, dw, TextSize::Small,
                            p.talents.Has(n->id) ? Palette::Highlight : Palette::TextDim) + 4.0f;
        if (!action.empty()) ui.TextWrapped(action, dx, y, dw, TextSize::Small, Palette::Xp);
    }

    const string technique = p.talents.Technique(style);
    string shown = technique;
    for (char& c : shown) if (c == '_') c = ' ';
    if (!shown.empty()) shown[0] = static_cast<char>(toupper(static_cast<unsigned char>(shown[0])));
    ui.Text("Charged attack: " + (shown.empty() ? string("plain") : shown),
            dx, panel.y + panel.h - 136.0f, TextSize::Small, Palette::TextDim);
    for (int slot = 0; slot < SkillTrees::ABILITY_SLOTS; ++slot) {
        const TalentNode* carried = p.talents.Ability(slot);
        ui.Text(input.PromptFor(input.ShiftAction()) + " + " + input.PromptFor(AbilityButton(slot)) +
                ": " + (carried ? carried->name : string("nothing carried")),
                dx, panel.y + panel.h - 114.0f + slot * 20.0f, TextSize::Small, carried ? SDL_Color{130, 190, 240, 255} : Palette::TextDim);
    }

    ui.Text(input.PromptFor(Action::Target) + " twice unlearn tree     " +
            input.PromptFor(Action::Back) + " close",
            panel.x + panel.w / 2.0f, panel.y + panel.h - 28.0f, TextSize::Small,
            tree_reset_armed ? SDL_Color{235, 190, 120, 255} : Palette::TextDim, Align::Center);
}
