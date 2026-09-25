// -----------------------------------------------------------------------------
//  World, continued: what the player can reach, open, gather, cook and pick up
//
//  The World class is one class in several files, cut along the sections it
//  always had: world.cpp has the map, the seats, sleep and the frame; the rest
//  is world_combat, world_projectiles, world_effects, world_interact and
//  world_render. Nothing but where a function lives changed when it was cut.
// -----------------------------------------------------------------------------
#include "world.h"
#include "../input.h"
#include "../systems/loot.h"
#include "../systems/quest.h"
#include "../systems/dialogue.h"
#include "../systems/spell.h"
#include "../systems/audio.h"
#include "../systems/gathering.h"

// Generous enough to reach anything the player can stand next to: a wide prop
// such as the mission board keeps them ~45px from its centre, so a tighter
// radius would leave them unable to use something they are leaning on.
static constexpr float INTERACT_RANGE = 58.0f;
static constexpr float PICKUP_RANGE   = 18.0f;
static constexpr float PICKUP_ARM     = 0.35f;   // no instant re-collect
// A dropped item is left alone until the player is this far from it.
static constexpr float DROP_CLEAR     = PICKUP_RANGE + 10.0f;

// -----------------------------------------------------------------------------
//  Interaction
// -----------------------------------------------------------------------------

void World::ResolveInteractTarget(const GameContext& ctx) {
    InteractTarget best;

    auto consider = [&](InteractTarget::Kind kind, int index,
                        const string& label, float tx, float ty) {
        const float d = Length(tx - player.x, ty - player.y);
        if (d > INTERACT_RANGE || d >= best.distance) return;
        best.kind = kind;
        best.index = index;
        best.label = label;
        best.distance = d;
    };

    for (size_t i = 0; i < npcs.size(); ++i)
        if (!npcs[i]->Away()) consider(InteractTarget::Npc, static_cast<int>(i),
                 "Talk to " + npcs[i]->Name() + (npcs[i]->Shop().empty() ? "" : "  -  trades"),
                 npcs[i]->x, npcs[i]->y);

    const auto& objects = map.Objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        const MapObject& o = objects[i];
        string label;

        if (!ObjectPresent(o)) continue;
        if (o.type == "chest") {
            label = Flagged(o.id) ? "" : "Open chest";
        } else if (o.type == "storage") {
            // A chest you keep things in rather than one you loot once, so it
            // never goes quiet after the first use.
            // The whole title lower-cased, not just its first letter the way
            // a workbench does it: a chest is named "Storage Chest" on the
            // panel that opens, and "Open the storage Chest" is not a sentence.
            string noun = o.title.empty() ? string("chest") : o.title;
            for (char& c : noun) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            label = "Open the " + noun;
        } else if (o.type == "search") {
            // Something to look under or behind. Says what it is before it is
            // searched and nothing afterwards, like a chest.
            label = Flagged(o.id) ? "" : (o.title.empty() ? "Search" : o.title);
        } else if (o.type == "lever") {
            label = Flagged(o.id) ? "" : (o.title.empty() ? "Use it" : o.title);
        } else if (o.type == "note") {
            label = "Read note";
        } else if (o.type == "board") {
            label = o.title.empty() ? "Read mission board" : ("Read " + o.title);
        } else if (o.type == "sign") {
            label = "Read sign";
        } else if (o.type == "bed") {
            label = clock.CanSleep() ? "Go to bed" : "Bed  -  you can sleep after dusk";
        } else if (o.type == "campsite") {
            label = clock.CanSleep() ? "Sleep by the fire" : "Campsite  -  you can sleep after dusk";
        } else if (o.type == "camp") {
            label = clock.CanSleep() ? "Sleep at your camp" : "Pack up your camp";
        } else if (o.type == "dream_wake") {
            label = "Touch the stone and wake";
        } else if (o.type == "range" || o.type == "workbench") {
            // Map titles are written as names ("Kitchen fire", "Anvil"), but
            // here they follow "the" mid-sentence.
            string noun = o.title.empty() ? string(o.type == "range" ? "fire" : o.station)
                                          : o.title;
            noun[0] = static_cast<char>(tolower(static_cast<unsigned char>(noun[0])));
            label = (o.type == "range" ? "Cook at the " : "Use the ") + noun;
        } else if (o.type == "waystone") {
            label = Flagged(o.id) ? "Touch the waystone" : "Wake the waystone";
        } else if (o.type == "curio") {
            label = ObjectPresent(o) ? "Pick up " + (o.title.empty() ? string("the thing") : o.title) : string();
        } else if (o.type == "totem_circle") {
            label = player.talents.PlacedTotem().empty() ? "Touch the ring"
                  : player.talents.TotemAwake()          ? "Touch the totem"
                                                         : "Touch the totem  -  it is asleep";
        } else if (o.type == "altar") {
            string noun = o.title.empty() ? string("enchanting table") : o.title;
            noun[0] = static_cast<char>(tolower(static_cast<unsigned char>(noun[0])));
            label = "Use the " + noun;
        } else if (o.type == "herb") {
            // A picked plant offers nothing until it has grown back.
            if (!Picked(o)) {
                if (player.skills.Level(SKILL_FORAGING) < o.skill_level)
                    label = "Needs Foraging " + std::to_string(o.skill_level) + " for the " + o.title;
                else
                    label = "Pick " + o.title;
            }
        } else if (o.type == "bug") {
            // Caught by hand, where it hangs over its spot: nothing to offer
            // once it is in somebody's jar, until another comes.
            if (!Picked(o)) {
                const string noun = o.title.empty() ? string("bug") : o.title;
                if (player.skills.Level(SKILL_FORAGING) < o.skill_level)
                    label = "Needs Foraging " + std::to_string(o.skill_level) + " to catch the " + noun;
                else
                    label = "Catch the " + noun;
            }
        } else if (o.type == "hive") {
            // A hive taken from gives nothing more until the bees have made
            // more.
            if (!Picked(o)) {
                const string noun = o.title.empty() ? string("hive") : o.title;
                if (player.skills.Level(SKILL_FORAGING) < o.skill_level)
                    label = "Needs Foraging " + std::to_string(o.skill_level) + " for the " + noun;
                else
                    label = "Take honey from the " + noun;
            }
        // A felled tree or a worked-out seam offers nothing either, until it
        // is back.
        } else if (!o.skill.empty() && !Spent(o)) {
            const int s = SkillFromName(o.skill);
            const bool fishing = o.skill == "Fishing";
            if (s >= 0 && player.skills.Level(s) < o.skill_level) {
                label = "Needs " + o.skill + " " + std::to_string(o.skill_level) +
                        (o.title.empty() ? string("") : " for the " + o.title);
            } else {
                label = (fishing ? "Fish the " : o.skill == "Mining" ? "Mine " : "Chop ") +
                        (o.title.empty() ? string(fishing ? "water" : "node") : o.title);
                // Say before the button is pressed that the tool is missing.
                const string tool = Gathering::ToolFor(o.skill);
                if (ctx.items && !Gathering::BestTool(player.inventory, player.equipment, *ctx.items,
                                                      player.skills, tool))
                    label += string("  -  needs ") + Gathering::ToolNoun(tool);
            }
        }

        if (!label.empty())
            consider(InteractTarget::Object, static_cast<int>(i), label, o.x, o.y);
    }

    // Doors are only offered when nothing closer wants the button.
    if (const Portal* p = map.PortalAt(player.BodyBox()))
        if (p->requires_interact) {
            const float cx = p->rect.x + p->rect.w / 2.0f;
            const float cy = p->rect.y + p->rect.h / 2.0f;
            // Say so at the door when what is inside outclasses the player; the
            // mine used to be found out about by dying in its first room.
            string label = p->label;
            if (p->min_combat > player.skills.CombatLevel())
                label += "  -  needs Combat " + std::to_string(p->min_combat);
            else if (p->danger_level > player.skills.CombatLevel())
                label += "  -  dangerous: Combat " + std::to_string(p->danger_level) + " advised";
            consider(InteractTarget::PortalDoor, 0, label, cx, cy);
        }

    player.interact = best;
}

void World::TryInteract(const GameContext& ctx) {
    if (player.IsDead() || transition_pending) return;
    if (visiting) {
        // The host does it, and says what came of it.
        visitor_acts.push_back({1, std::to_string(static_cast<int>(player.interact.kind)), "", player.interact.index});
        return;
    }
    // Lying down, E gets up.
    if (player.resting) { player.resting = false; return; }

    // Interrupting a gather is what the button does while one is running.
    if (gather_index >= 0) { gather_index = -1; return; }

    const InteractTarget& t = player.interact;

    switch (t.kind) {
        case InteractTarget::Npc: {
            if (t.index < 0 || t.index >= static_cast<int>(npcs.size())) break;
            Npc& npc = *npcs[t.index];
            npc.FaceToward(player.x, player.y);

            if (npc.DialogueRoot().empty()) {
                AddText("...", npc.x, npc.y - 48.0f, {200, 200, 210, 255});
                break;
            }
            WorldRequest r;
            r.type  = WorldRequest::Type::Dialogue;
            r.id    = npc.Id();
            r.title = npc.Name();
            r.text  = npc.DialogueRoot();
            requests.push_back(r);
            break;
        }

        case InteractTarget::Object: {
            const auto& objects = map.Objects();
            if (t.index < 0 || t.index >= static_cast<int>(objects.size())) break;
            const MapObject& o = objects[t.index];

            if (o.type == "lever") {
                if (Flagged(o.id) || !ObjectPresent(o)) break;
                SetFlag(o.id);
                Audio::Play(Sfx::ChestOpen);
                AddText(o.text.empty() ? "It gives." : o.text, o.x, o.y - 34.0f, {200, 235, 255, 255}, 2.4f);
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    ctx.quests->Notify(e, player.inventory);
                }
                break;
            }
            if (o.type == "totem_circle") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Totem;
                r.id    = o.id;
                r.title = o.title.empty() ? "The ring" : o.title;
                requests.push_back(r);
                Audio::Play(Sfx::UiConfirm);
                break;
            }
            if (o.type == "storage") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Storage;
                r.id    = o.id;
                r.title = o.title.empty() ? "Storage Chest" : o.title;
                r.count = o.capacity > 0 ? o.capacity : 100;
                requests.push_back(r);
                Audio::Play(Sfx::ChestOpen);
                break;
            }
            if (o.type == "search") {
                if (Flagged(o.id) || !ObjectPresent(o)) break;
                SetFlag(o.id);
                Audio::Play(Sfx::ChestOpen);
                if (!o.loot_table.empty()) SpawnLoot(o.loot_table, o.x, o.y + 10.0f, ctx);
                if (!o.loot_item.empty())
                    DropItem(o.loot_item, std::max(1, o.loot_qty), o.x, o.y + 10.0f, ctx);
                AddText(o.text.empty() ? "There is something under it." : o.text,
                        o.x, o.y - 30.0f, {255, 225, 120, 255}, 2.4f);
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    e.map_id = map_id;
                    ctx.quests->Notify(e, player.inventory);
                }
                break;
            }
            if (o.type == "chest") {
                if (Flagged(o.id) || !ObjectPresent(o)) break;
                SetFlag(o.id);
                Audio::Play(Sfx::ChestOpen);
                if (!o.loot_table.empty()) SpawnLoot(o.loot_table, o.x, o.y + 10.0f, ctx);
                // What a chest holds by name, which no loot table can roll.
                if (!o.loot_item.empty())
                    DropItem(o.loot_item, std::max(1, o.loot_qty), o.x, o.y + 10.0f, ctx);
                AddText("Opened!", o.x, o.y - 34.0f, {255, 225, 120, 255});
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    ctx.quests->Notify(e, player.inventory);
                }
            } else if (o.type == "bed" || o.type == "campsite") {
                AskToSleep(!o.title.empty() ? o.title : string(o.type == "bed" ? "A bed for the night" : "By the fire"),
                           o.fee);
            } else if (o.type == "camp") {
                if (clock.CanSleep()) {
                    AskToSleep("Your camp");
                } else if (player.inventory.Full()) {
                    AddText("No room in your pack for the bedroll.", player.x, player.y - 54.0f,
                            {255, 170, 150, 255}, 1.6f);
                    Audio::Play(Sfx::UiError);
                } else {
                    camp = {};
                    PlaceCampObjects();
                    player.inventory.Add("bedroll", 1);
                    AddText("Camp packed away.", player.x, player.y - 54.0f, {220, 220, 200, 255});
                    Audio::Play(Sfx::Pickup);
                }
            } else if (o.type == "dream_wake") {
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    e.map_id = map_id;
                    ctx.quests->Notify(e, player.inventory);
                }
                Wake(WakeReason::Stone);
            } else if (o.type == "range" || o.type == "workbench") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Craft;
                r.id    = o.id;
                r.text  = o.type == "range" ? string("range") : o.station;
                r.title = !o.title.empty() ? o.title
                        : o.type == "range" ? string("Cooking fire")
                        : o.station == "anvil" ? string("Anvil")
                        : o.station == "loom" ? string("Loom")
                        : o.station == "rack" ? string("Tanning Rack") : string("Workbench");
                requests.push_back(r);
            } else if (o.type == "curio") {
                // Straight into the bag: it is the having of it that starts
                // what it starts (see Game::NoticeFinds), and a thing kicked
                // across the ground first would only be a second thing to find.
                if (!ObjectPresent(o) || o.loot_item.empty()) break;
                const int got = player.inventory.Add(o.loot_item, std::max(1, o.loot_qty));
                if (got <= 0) {
                    AddText("No room in the bag.", o.x, o.y - 30.0f, {255, 140, 140, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                }
                Audio::Play(Sfx::Pickup);
                if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    e.map_id = map_id;
                    ctx.quests->Notify(e, player.inventory);
                }
            } else if (o.type == "waystone") {
                if (!Flagged(o.id)) {
                    // The first hand on it wakes it, and that is all the first
                    // touch does: a stone is found before it is used.
                    SetFlag(o.id);
                    Burst(o.x, o.y - 40.0f, 90.0f, {150, 220, 255, 255}, 26);
                    // A ring through the air round it, a pale flash, and its
                    // light going up off it in motes.
                    Shock(o.x, o.y - 20.0f, 0.5f, 0.0f);
                    Flash({170, 228, 255, 255}, 0.28f);
                    for (int i = 0; i < 28; ++i) {
                        Mote m;
                        const float a = 6.2831853f * i / 28.0f;
                        m.x = o.x + cosf(a) * (10.0f + (i % 5) * 4.0f);
                        m.y = o.y - 6.0f + sinf(a) * 5.0f;
                        m.vx = cosf(a) * 8.0f;
                        m.vy = -26.0f - (i % 7) * 6.0f;
                        m.gravity = -12.0f;
                        m.drag = 0.6f;
                        m.life = m.max_life = 1.1f + (i % 4) * 0.25f;
                        m.size = (i % 3 == 0) ? 2.0f : 1.0f;
                        m.lift = LiftAt(o.x, o.y);
                        m.from = {200, 240, 255, 255};
                        m.to = {140, 200, 255, 0};
                        motes.push_back(m);
                    }
                    AddText("The waystone wakes", o.x, o.y - 84.0f, {170, 228, 255, 255}, 2.4f);
                    Audio::PlayAt(Sfx::QuestStart, o.x, o.y);
                    WorldRequest r;
                    r.type = WorldRequest::Type::Toast;
                    r.text = "The waystone is awake. Touch it again to go to any other you have woken.";
                    requests.push_back(r);
                } else {
                    WorldRequest r;
                    r.type = WorldRequest::Type::Travel;
                    r.id   = o.id;
                    requests.push_back(r);
                }
            } else if (o.type == "altar") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Enchant;
                r.id    = o.id;
                r.title = o.title.empty() ? "Enchanting Table" : o.title;
                requests.push_back(r);
            } else if (o.type == "note" || o.type == "sign") {
                // Reading something can be what a quest asks for.
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    e.map_id = map_id;
                    ctx.quests->Notify(e, player.inventory);
                }
                // A note can also leave something behind, but only once.
                if (!o.loot_table.empty() && !Flagged(o.id))
                    SpawnLoot(o.loot_table, o.x, o.y + 8.0f, ctx);

                WorldRequest r;
                r.type  = WorldRequest::Type::Note;
                r.id    = o.id;
                r.title = o.title.empty() ? (o.type == "sign" ? "Sign" : "A scrawled note") : o.title;
                r.text  = o.text;
                r.list  = o.starts_quest.empty() ? vector<string>{}
                                                 : vector<string>{o.starts_quest};
                requests.push_back(r);
                SetFlag(o.id);
            } else if (o.type == "board") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Board;
                r.id    = o.id;
                r.title = o.title.empty() ? "Mission Board" : o.title;
                r.list  = o.quests;
                requests.push_back(r);
            } else if (o.type == "herb" || o.type == "bug" || o.type == "hive") {
                // A plant picked, a bug caught, honey taken: all by hand, all
                // Foraging, all the same short piece of work.
                if (Picked(o)) break;
                if (player.skills.Level(SKILL_FORAGING) < o.skill_level) {
                    AddText("Foraging " + std::to_string(o.skill_level) + " needed", o.x, o.y - 30.0f,
                            {255, 140, 140, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                }
                // Picked by hand: no tool, and the level alone sets the pace.
                gather_index  = t.index;
                gather_timer  = 0.0f;
                gather_needed = Gathering::WorkTime(o.gather_time, player.skills.Level(SKILL_FORAGING), 1.0f);
                player.StartGathering("gather", "", o.x, o.y);
            } else if (!o.skill.empty()) {
                const int s = SkillFromName(o.skill);
                if (s < 0 || !ctx.items) break;
                if (Spent(o)) break;
                if (player.skills.Level(s) < o.skill_level) {
                    AddText("Level too low", o.x, o.y - 34.0f, {255, 140, 140, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                }
                // No work without the tool for it: the fastest one carried that
                // the player has the level to use.
                const string tool_kind = Gathering::ToolFor(o.skill);
                const ItemDef* locked = nullptr;
                const ItemDef* tool = Gathering::BestTool(player.inventory, player.equipment, *ctx.items,
                                                          player.skills, tool_kind, &locked);
                if (!tool) {
                    string why = string("You need ") + Gathering::ToolNoun(tool_kind) + ".";
                    if (locked)
                        for (const auto& req : locked->requirements)
                            why = "Your " + locked->name + " needs " + SkillName(req.first) + " " +
                                  std::to_string(req.second) + ".";
                    AddText(why, player.x, player.y - 54.0f, {255, 170, 150, 255}, 1.8f);
                    Audio::Play(Sfx::UiError);
                    break;
                }
                gather_index  = t.index;
                gather_timer  = 0.0f;
                // The level and the tool together decide the pace, down to a floor.
                gather_needed = Gathering::WorkTime(o.gather_time, player.skills.Level(s), tool->tool_speed);
                player.StartGathering(Gathering::ClipFor(o.skill), tool->model, o.x, o.y);
                if (o.skill == "Fishing") Audio::PlayAt(Sfx::Splash, o.x, o.y);
            }
            break;
        }

        case InteractTarget::PortalDoor: {
            const Portal* p = map.PortalAt(player.BodyBox());
            if (!p) break;
            if (!p->locked_by.empty() && !player.inventory.Has(p->locked_by)) {
                AddText("It is locked.", player.x, player.y - 52.0f, {255, 150, 150, 255});
                Audio::Play(Sfx::Locked);
                break;
            }
            if (p->min_combat > player.skills.CombatLevel()) {
                AddText("Too dangerous for you yet: Combat " + std::to_string(p->min_combat) + " needed.",
                        player.x, player.y - 52.0f, {255, 150, 150, 255}, 2.2f);
                Audio::Play(Sfx::Locked);
                break;
            }
            if (RequestTransition(p->target_map, p->target_spawn)) Audio::Play(Sfx::Door);
            break;
        }

        default: break;
    }
}

// Cooking works the way the rest of the skills do: stand at a fire, press the
// button, turn one raw thing into one cooked thing. Burning is possible until
// the level is comfortably above the recipe.
void World::CookOne(const MapObject& range, const GameContext& ctx) {
    if (!ctx.items) return;

    const int level = player.skills.Level(SKILL_COOKING);
    // The lowest requirement among raw things the player cannot cook yet, so
    // the refusal can name the level that would actually help.
    int needed = 0;

    for (int slot = 0; slot < player.inventory.SlotCount(); ++slot) {
        const ItemStack& stack = player.inventory.Slot(slot);
        if (stack.Empty()) continue;

        const ItemDef* def = ctx.items->Get(stack.id);
        if (!def || def->cook_result.empty()) continue;

        // Skip what is beyond the player rather than stopping at it. This used
        // to give up at the first raw item in the bag, so a boar haunch
        // (Cooking 12) sitting ahead of plain raw meat (Cooking 1) meant a new
        // character could cook nothing at all.
        if (level < def->cook_level) {
            needed = (needed == 0) ? def->cook_level : std::min(needed, def->cook_level);
            continue;
        }

        player.inventory.RemoveSlot(slot, 1);

        // Chance to burn falls away as the level climbs past the requirement.
        const int margin = level - def->cook_level;
        std::uniform_real_distribution<float> roll(0.0f, 1.0f);
        const float burn_chance = std::max(0.0f, 0.34f - margin * 0.03f);

        if (ctx.rng && roll(*ctx.rng) < burn_chance) {
            AddText("Burnt!", player.x, player.y - 54.0f, {200, 110, 90, 255});
            Audio::Play(Sfx::Burn);
            player.GrantXp(SKILL_COOKING, std::max(1, def->cook_xp / 8));
            return;
        }

        player.inventory.Add(def->cook_result, 1);
        player.GrantXp(SKILL_COOKING, def->cook_xp);
        Audio::Play(Sfx::Cook);

        const ItemDef* cooked = ctx.items->Get(def->cook_result);
        AddText("+ " + (cooked ? cooked->name : def->cook_result),
                player.x, player.y - 54.0f, {200, 255, 200, 255});
        if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
        return;
    }

    if (needed > 0)
        AddText("Cooking " + std::to_string(needed) + " needed",
                range.x, range.y - 34.0f, {255, 150, 150, 255});
    else
        AddText("Nothing raw to cook", range.x, range.y - 34.0f, {200, 200, 210, 255});
}

Inventory& World::Storage(const string& object_id, int slots, const ItemDatabase* db) {
    auto it = storage.find(object_id);
    if (it == storage.end())
        it = storage.emplace(object_id, Inventory(db, std::max(1, slots))).first;
    // A chest that has been saved and loaded comes back without a database,
    // and one whose capacity has been changed in the data comes back the old
    // size. Both are fixed here rather than at load, so there is one place
    // that knows what a chest is supposed to be.
    it->second.SetDatabase(db);
    it->second.Resize(std::max(1, slots));
    return it->second;
}

bool World::Picked(const MapObject& o) const {
    auto it = picked.find(map_id + ":" + o.id);
    return it != picked.end() && GameHours() < it->second;
}

void World::Pick(const MapObject& o) {
    picked[map_id + ":" + o.id] = GameHours() + o.regrow_hours;
    if (journal) picked_log.push_back({map_id + ":" + o.id, GameHours() + o.regrow_hours});
    // Anything already grown back is dropped, so the save does not keep
    // every herb ever picked.
    for (auto it = picked.begin(); it != picked.end();)
        it = (GameHours() >= it->second) ? picked.erase(it) : std::next(it);
}

bool World::Spent(const MapObject& o) const {
    return o.deplete > 0.0f && Picked(o);
}

void World::UpdateGathering(float dt, const GameContext& ctx) {
    if (gather_index < 0) return;

    const auto& objects = map.Objects();
    if (gather_index >= static_cast<int>(objects.size())) { gather_index = -1; return; }
    const MapObject& o = objects[gather_index];

    // Walking away cancels it, and so does walking at all: work is done
    // standing still.
    if (Length(o.x - player.x, o.y - player.y) > INTERACT_RANGE + 12.0f || player.Moving() ||
        player.Attacking() || player.IsJumping()) {
        gather_index = -1;
        return;
    }

    // A strike every so often while the work goes on, not just at the end.
    // Fishing is quiet until something bites, and so is picking -- a plant, a
    // bug out of the air, honey out of a hive.
    const bool by_hand = o.type == "herb" || o.type == "bug" || o.type == "hive";
    const bool fishing = o.skill == "Fishing" || by_hand;
    constexpr float STRIKE = 0.62f;
    const float before = gather_timer;
    gather_timer += dt;
    if (!fishing && (std::floor(before / STRIKE) != std::floor(gather_timer / STRIKE) || before == 0.0f))
        Audio::PlayAt(o.skill == "Mining" ? Sfx::Mine : Sfx::Chop, o.x, o.y);
    if (gather_timer < gather_needed) return;

    const int skill = SkillFromName(o.skill);

    // A herb is picked once, then grows back. The further past its level the
    // forager is, the more often a plant gives two. A bug is caught the same
    // way and another comes to the spot after a while; a hive gives its honey
    // and is left to make more.
    if (by_hand) {
        const ItemDef* d = ctx.items ? ctx.items->Get(o.yield) : nullptr;
        int count = 1;
        if (ctx.rng) {
            const float extra = Gathering::ForageExtraChance(player.skills.Level(SKILL_FORAGING), o.skill_level);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            if (unit(*ctx.rng) < extra) count = 2;
        }
        const int added = player.inventory.Add(o.yield, count);
        if (added <= 0) {
            AddText("Inventory full", player.x, player.y - 54.0f, {255, 160, 160, 255});
            gather_index = -1;
            return;
        }
        // A herb pays its own XP. A bug or a hive pays what its spot says
        // (genmaps copies a bug's from its catch block) -- or, for a bug
        // whose spot says nothing, the bug's own.
        int each = o.yield_xp;
        if (o.type == "herb" && d && d->forage_xp > 0) each = d->forage_xp;
        if (o.type == "bug" && each <= 0 && d) each = d->catch_xp;
        player.GrantXp(SKILL_FORAGING, each * added);
        AddText("+ " + (added > 1 ? std::to_string(added) + " " : string("")) + (d ? d->name : o.yield),
                player.x, player.y - 54.0f, added > 1 ? SDL_Color{255, 230, 140, 255} : SDL_Color{200, 255, 200, 255});
        Audio::PlayAt(Sfx::Pickup, o.x, o.y);
        Pick(o);
        if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
        gather_index = -1;
        return;
    }

    if (fishing && ctx.items && ctx.rng) {
        const int level = player.skills.Level(SKILL_FISHING);
        const string fish = Gathering::PickFish(o.fish, level, *ctx.items, *ctx.rng);
        const ItemDef* d = ctx.items->Get(fish);
        if (!d) { gather_index = -1; return; }
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const int count = Gathering::CatchCount(level, unit(*ctx.rng));
        const int added = player.inventory.Add(fish, count);
        if (added <= 0) {
            AddText("Inventory full", player.x, player.y - 54.0f, {255, 160, 160, 255});
            gather_index = -1;
            return;
        }
        player.GrantXp(SKILL_FISHING, d->fish_xp * added);
        AddText("+ " + (count > 1 ? std::to_string(count) + " " : string("")) + d->name,
                player.x, player.y - 54.0f, count > 1 ? SDL_Color{255, 230, 140, 255} : SDL_Color{200, 255, 200, 255});
        Audio::PlayAt(Sfx::Splash, o.x, o.y, 1.0f, 1.2f);
        if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
        gather_timer = 0.0f;
        return;
    }

    // The log goes in the bag before the swing is paid for. It used to be the
    // other way round: with a full pack every swing still paid its experience,
    // and because the refusal below returned before the dice were rolled the
    // tree never came down either -- a full bag made any seam an endless one.
    if (!o.yield.empty()) {
        const int got = player.inventory.Add(o.yield, 1 + DreamBonus(o.yield));
        if (got > 0) {
            if (skill >= 0 && o.yield_xp > 0) player.GrantXp(skill, o.yield_xp);
            const ItemDef* d = ctx.items ? ctx.items->Get(o.yield) : nullptr;
            AddText("+ " + (got > 1 ? std::to_string(got) + " " : string("")) + (d ? d->name : o.yield),
                    player.x, player.y - 54.0f, {200, 255, 200, 255});
            if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
        } else {
            AddText("Inventory full", player.x, player.y - 54.0f, {255, 160, 160, 255});
            gather_index = -1;
            return;
        }
    } else if (skill >= 0 && o.yield_xp > 0) {
        // Something worked for its own sake, with nothing to carry away.
        player.GrantXp(skill, o.yield_xp);
    }

    // The dice roll. A tree does not stand there giving logs for ever: on
    // each one there is a chance it comes down, and on each ore a chance the
    // seam gives out. Then it is gone for a while and the work stops.
    if (o.deplete > 0.0f && ctx.rng) {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        if (Gathering::Depletes(o.deplete, unit(*ctx.rng))) {
            Pick(o);
            const bool tree = o.type == "tree";
            AddText(tree ? "The tree comes down." : "The seam is worked out.",
                    o.x, o.y - 30.0f, {220, 220, 200, 255}, 1.8f);
            Audio::PlayAt(tree ? Sfx::Land : Sfx::Mine, o.x, o.y, 1.0f, 0.6f);
            gather_index = -1;
            return;
        }
    }

    // Keep going until the player moves or presses the button again.
    gather_timer = 0.0f;
}

void World::ShowGather(float progress, const string& clip, const string& model) {
    if (clip.empty()) {
        if (shown_gather > 0.0f || !player.GatherClip().empty()) player.StopGathering();
        shown_gather = 0.0f;
        return;
    }
    if (player.GatherClip() != clip) {
        const float dx = player.facing == FACE_LEFT ? -1.0f : player.facing == FACE_RIGHT ? 1.0f : 0.0f;
        const float dy = player.facing == FACE_UP ? -1.0f : player.facing == FACE_DOWN ? 1.0f : 0.0f;
        player.StartGathering(clip, model, player.x + dx * 24.0f, player.y + dy * 24.0f);
    }
    shown_gather = std::max(0.001f, progress);
}

float World::GatherProgress() const {
    if (visiting) return std::clamp(shown_gather, 0.0f, 1.0f);
    if (gather_index < 0 || gather_needed <= 0.0f) return 0.0f;
    return std::clamp(gather_timer / gather_needed, 0.0f, 1.0f);
}

// -----------------------------------------------------------------------------
//  Loot and floating text
// -----------------------------------------------------------------------------

void World::SpawnLoot(const string& table_id, float x, float y, const GameContext& ctx) {
    if (!ctx.loot) return;
    vector<LootDrop> drops = ctx.loot->Roll(table_id);
    // Deeper in the dream there is more of it: once for whatever this was, on
    // the first of its shards, not once a stack.
    for (auto& d : drops)
        if (const int more = DreamBonus(d.item)) { d.qty += more; break; }

    int index = 0;
    for (const auto& d : drops) {
        // Fan the pile out so overlapping drops stay clickable.
        const float angle = 1.9f * index;
        const float radius = drops.size() > 1 ? 9.0f + 3.0f * index : 0.0f;
        DropItem(d.item, d.qty, x + cosf(angle) * radius, y + sinf(angle) * radius * 0.6f, ctx);
        ++index;
    }
}

void World::DropItem(const string& item_id, int qty, float x, float y,
                     const GameContext& ctx, bool by_player) {
    if (item_id.empty() || qty <= 0) return;
    if (visiting) {
        if (by_player) visitor_acts.push_back({2, item_id, "", qty});
        return;
    }
    Pickup p;
    p.net_id = next_net_id++;
    p.dropper_seat = player.seat;
    p.item_id = item_id;
    p.qty     = qty;
    p.dropped = by_player;
    p.x = x;
    p.y = y;
    if (const ItemDef* d = ctx.items ? ctx.items->Get(item_id) : nullptr) p.icon = d->icon;
    pickups.push_back(p);
}

void World::UpdatePickups(float dt, const GameContext& ctx) {
    (void)ctx;
    for (auto& p : pickups) {
        p.life += dt;
        p.bob  += dt * 3.4f;
        if (p.dropped && p.life > DROP_LIFE) p.collected = true;
    }
    pickups.erase(std::remove_if(pickups.begin(), pickups.end(),
                                 [](const Pickup& p) { return p.collected; }),
                  pickups.end());
}

void World::CollectPickups(float dt, const GameContext& ctx) {
    for (auto& p : pickups) {
        if (p.collected || p.life < PICKUP_ARM) continue;
        if (player.IsDead()) continue;
        const float dist = Length(p.x - player.x, p.y - player.y);
        if (p.dropped && !p.cleared && p.dropper_seat == player.seat) {
            // Whoever put it down steps clear of it once before it can be
            // theirs again. Anyone else may have it: that is how things
            // change hands.
            if (dist > DROP_CLEAR) p.cleared = true;
            continue;
        }
        if (dist > PICKUP_RANGE) continue;

        const int added = player.inventory.Add(p.item_id, p.qty);
        if (added <= 0) {
            // Say so once every couple of seconds rather than every frame.
            if (fmodf(p.life, 2.0f) < dt)
                AddText("Inventory full", player.x, player.y - 54.0f, {255, 160, 160, 255});
            continue;
        }

        const ItemDef* d = ctx.items ? ctx.items->Get(p.item_id) : nullptr;
        const string name = d ? d->name : p.item_id;
        AddText("+" + std::to_string(added) + " " + name, player.x, player.y - 50.0f,
                {230, 230, 255, 255});
        Audio::Play(p.item_id == "coins" ? Sfx::Coins : Sfx::Pickup);

        p.collected = true;
        if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
    }

    pickups.erase(std::remove_if(pickups.begin(), pickups.end(),
                                 [](const Pickup& p) { return p.collected; }),
                  pickups.end());
}

void World::AddText(const string& text, float x, float y, SDL_Color color, float life) {
    FloatingText t;
    t.text = text;
    t.x = x;
    // Lifted with whoever it is about. Text is put forty-odd pixels over
    // someone's feet, so the ground asked about is the ground under those. A
    // guest's window is told where the host already put it.
    t.y = visiting ? y : y - LiftAt(x, y + 46.0f);
    t.life = t.max_life = life;
    t.color = color;
    texts.push_back(t);
    if (journal) text_log.push_back(t);
}

void World::UpdateTexts(float dt) {
    for (auto& t : texts) t.life -= dt;
    texts.erase(std::remove_if(texts.begin(), texts.end(),
                               [](const FloatingText& t) { return t.life <= 0.0f; }),
                texts.end());
}

vector<WorldRequest> World::TakeRequests() {
    vector<WorldRequest> out;
    out.swap(requests);
    return out;
}
