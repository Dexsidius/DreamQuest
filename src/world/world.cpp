#include "world.h"
#include "../input.h"
#include "../systems/loot.h"
#include "../systems/quest.h"
#include "../systems/dialogue.h"
#include "../systems/spell.h"
#include "../systems/audio.h"

static constexpr float FADE_SPEED     = 3.2f;
// Generous enough to reach anything the player can stand next to: a wide prop
// such as the mission board keeps them ~45px from its centre, so a tighter
// radius would leave them unable to use something they are leaning on.
static constexpr float INTERACT_RANGE = 58.0f;
static constexpr float PICKUP_RANGE   = 18.0f;
static constexpr float PICKUP_ARM     = 0.35f;   // no instant re-collect

// -----------------------------------------------------------------------------
//  Map loading and transitions
// -----------------------------------------------------------------------------

bool World::LoadMap(const string& id, const string& spawn, const GameContext& ctx) {
    const string path = "maps/" + id + ".mx";
    // Load into a candidate first. A missing or malformed destination must not
    // unload the area the player is still standing in.
    Map arriving;
    try {
        if (!arriving.Load(path)) return false;
    } catch (const std::exception& e) {
        SDL_Log("World: invalid destination '%s': %s", id.c_str(), e.what());
        return false;
    }
    map = std::move(arriving);

    map_id = id;
    enemies.clear();
    npcs.clear();
    pickups.clear();
    texts.clear();
    projectiles.clear();
    ground_effects.clear();
    impacts.clear();
    gather_index = -1;

    SpawnEntitiesFromMap(ctx);

    SDL_FPoint p;
    if (spawn.empty() || !map.Spawn(spawn, p)) p = map.DefaultSpawn();
    player.x = p.x;
    player.y = p.y;
    player.knock_x = player.knock_y = 0.0f;
    portals_armed = false;
    arrival_released = false;

    camera.SetBounds(map.Width(), map.Height());
    camera.SnapTo(player.x, player.y);
    ambience.SetKind(map.Ambient(), map.IsInterior());
    Audio::SetAmbience(map.Ambient(), map.IsInterior());
    Audio::SetListener(player.x, player.y);
    if (ctx.quests) {
        QuestEvent e;
        e.type = ObjectiveType::Reach;
        e.target = map_id;
        e.map_id = map_id;
        ctx.quests->Notify(e, player.inventory);
    }
    return true;
}

void World::SpawnEntitiesFromMap(const GameContext& ctx) {
    for (const auto& def : map.Enemies()) {
        // A monster already killed this session stays dead until its timer
        // brings it back; flags cover the permanent ones.
        const EnemyDef* stats = ctx.enemies ? ctx.enemies->Get(def.type) : nullptr;
        if (!stats) {
            SDL_Log("World: unknown enemy type '%s'", def.type.c_str());
            continue;
        }
        auto e = std::make_unique<Enemy>();
        e->Init(stats, def, ctx);
        enemies.push_back(std::move(e));
    }

    for (const auto& def : map.Npcs()) {
        auto n = std::make_unique<Npc>();
        n->Init(def, ctx);
        npcs.push_back(std::move(n));
    }
}

void World::RequestTransition(const string& id, const string& spawn) {
    if (transition_pending) return;
    transition_pending = true;
    next_map   = id;
    next_spawn = spawn;
    fade_dir   = 1;
}

void World::ApplyTransition(const GameContext& ctx) {
    if (!LoadMap(next_map, next_spawn, ctx)) {
        SDL_Log("World: failed to enter map '%s'", next_map.c_str());
        WorldRequest r;
        r.type = WorldRequest::Type::Toast;
        r.text = "That path could not be opened. Your current area is unchanged.";
        requests.push_back(r);
        // Do not retry a broken exit every frame while standing on it.
        portals_armed = false;
        arrival_released = false;
    }
    transition_pending = false;
    fade_dir = -1;
}

// -----------------------------------------------------------------------------
//  Frame update
// -----------------------------------------------------------------------------

void World::Update(float dt, const GameContext& ctx) {
    // --- screen wipe ---------------------------------------------------------
    if (fade_dir != 0) {
        fade += fade_dir * FADE_SPEED * dt;
        if (fade_dir > 0 && fade >= 1.0f) {
            fade = 1.0f;
            if (transition_pending) ApplyTransition(ctx);
        } else if (fade_dir < 0 && fade <= 0.0f) {
            fade = 0.0f;
            fade_dir = 0;
        }
    }
    // Movement stays frozen while the screen is covered, but only for as long
    // as it is covered: Game owns input_locked for open panels, so borrow it
    // and hand it back rather than latching it on.
    const bool frozen = (fade_dir > 0 && transition_pending);
    const bool locked_by_game = player.input_locked;
    player.input_locked = locked_by_game || frozen;

    player.Update(dt, *this, ctx);

    player.input_locked = locked_by_game;

    if (!player.IsDead()) {
        ApplyPlayerAttack(ctx);
        ResolveInteractTarget(ctx);
        UpdateGathering(dt, ctx);

        // Step-through portals fire without a button press.
        //
        // But not for a walk that began on the previous map. Arrival spawns
        // sit a pace or two from the way back -- forty pixels on the field
        // outside Havenbrook -- so holding a direction through the fade used
        // to carry the player straight into the return portal and bounce them
        // back where they came from, over and over for as long as the key was
        // down. So the portals wait until the key has been let go -- and until
        // the player is standing clear of them, because letting go a step
        // too late leaves you on top of the way back, and arming it there
        // bounced you just the same.
        if (!portals_armed) {
            if (!ctx.input || Length(ctx.input->MoveAxis().x, ctx.input->MoveAxis().y) < 0.01f)
                arrival_released = true;
            if (arrival_released && !map.PortalAt(player.Bounds()))
                portals_armed = true;
        }

        if (!transition_pending && portals_armed) {
            if (const Portal* p = map.PortalAt(player.Bounds()))
                if (!p->requires_interact && p->locked_by.empty()) {
                    RequestTransition(p->target_map, p->target_spawn);
                    Audio::Play(Sfx::Portal);
                }
        }
    } else {
        player.interact = {};
        gather_index = -1;
    }

    for (auto& e : enemies) {
        if (e->CurrentState() == Enemy::State::Dead) {
            e->TickRespawn(dt);
            // Not while the player is standing on its spawn point. A boar
            // killed where it grazed came back in the same spot a minute later,
            // already inside its aggro range, and a player who had stopped to
            // open their bag was dead before they closed it. Wait until they
            // are clear of where it would start chasing them.
            const float clearance = e->Def() ? std::max(192.0f, e->Def()->aggro_range + 64.0f)
                                             : 192.0f;
            const bool player_near = !player.IsDead() &&
                Length(e->home_x - player.x, e->home_y - player.y) < clearance;
            if (e->ReadyToRespawn() && !player_near) e->Revive();
            else                     e->Update(dt, *this, ctx);
            continue;
        }
        e->Update(dt, *this, ctx);
    }

    for (auto& n : npcs) n->Update(dt, *this, ctx);

    UpdateProjectiles(dt, ctx);
    UpdateGroundEffects(dt, ctx);
    UpdateImpacts(dt);
    UpdateElevation();
    UpdatePickups(dt, ctx);
    UpdateTexts(dt);

    camera.Follow(player.x, player.y, dt);
    ambience.Update(dt, camera);
    Audio::SetListener(player.x, player.y);
}

// -----------------------------------------------------------------------------
//  Combat resolution
// -----------------------------------------------------------------------------

// Where a shot or a cast is aimed. With a mouse the cursor is the natural
// answer; on a controller the character shoots the way they are facing.
Vec2 World::PlayerAim(const GameContext& ctx) const {
    if (ctx.input && ctx.input->ActiveDevice() == InputMode::KeyboardMouse) {
        const SDL_FPoint m = ctx.input->MousePos();
        const SDL_FPoint w = camera.ToWorld(m.x, m.y);
        const float dx = w.x - player.x;
        const float dy = w.y - (player.y - 16.0f);
        const float len = Length(dx, dy);
        if (len > 4.0f) return {dx / len, dy / len};
    }
    switch (player.facing) {
        case FACE_UP:    return {0.0f, -1.0f};
        case FACE_DOWN:  return {0.0f,  1.0f};
        case FACE_LEFT:  return {-1.0f, 0.0f};
        default:         return {1.0f,  0.0f};
    }
}

// A bow or a staff turns the same attack button into a shot or a cast. The
// swing animation and its timing are unchanged; only what leaves the character
// at the active frame is different.
void World::FirePlayerProjectile(const GameContext& ctx) {
    const AttackState& atk = player.Attack();
    const AttackStyle style = player.Style();
    const Vec2 aim = PlayerAim(ctx);

    string projectile_id;
    float damage_mult = atk.damage_mult;

    if (style == AttackStyle::Ranged) {
        projectile_id = "arrow";
    } else {
        const SpellDef* spell = ctx.spells
            ? ctx.spells->BestFor(player.SelectedElement(),
                                  player.skills.Level(SKILL_MAGIC))
            : nullptr;
        if (!spell) {
            AddText("No spell known", player.x, player.y - 54.0f, {200, 200, 210, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        if (!player.SpendMana(spell->mana)) {
            AddText("Out of mana", player.x, player.y - 54.0f, {150, 180, 235, 255});
            Audio::Play(Sfx::UiError);
            return;
        }
        projectile_id = spell->projectile;
        damage_mult *= spell->damage_mult;
        // Casting trains Magic whether or not the bolt finds anything.
        player.GrantXp(SKILL_MAGIC, spell->xp);
    }

    if (style == AttackStyle::Ranged) {
        Audio::Play(Sfx::BowShot);
    } else {
        // Each element is pitched a little differently.
        const Element el = player.SelectedElement();
        const float pitch = el == Element::Fire ? 0.9f : el == Element::Water ? 1.1f
                          : el == Element::Earth ? 0.75f : 1.25f;
        Audio::Play(Sfx::SpellCast, 1.0f, pitch);
    }

    // Leave from chest height, slightly ahead so it clears the caster.
    SpawnProjectile(projectile_id,
                    player.x + aim.x * 12.0f, player.y - 16.0f + aim.y * 12.0f,
                    aim.x, aim.y, player.Profile(), style,
                    damage_mult, true, ctx);
}

void World::ApplyPlayerAttack(const GameContext& ctx) {
    // One swing lands once, on every enemy inside the arc.
    if (!player.AttackPending()) return;
    player.MarkAttackConsumed();
    const AttackState& atk = player.Attack();

    if (player.Style() != AttackStyle::Melee) {
        FirePlayerProjectile(ctx);
        return;
    }

    const SDL_FRect hit = AttackHitbox(player.x, player.y, player.facing,
                                       atk.profile, atk.reach_scale);
    bool connected = false;

    for (auto& e : enemies) {
        if (e->Dead() || e->CurrentState() == Enemy::State::Dead) continue;
        if (!RectsOverlap(hit, e->BodyBox())) continue;

        connected = true;
        HitEnemy(*e, player.Profile(), AttackStyle::Melee, Element::None,
                 atk.damage_mult, atk.profile.knockback,
                 player.x, player.y, ctx);
    }

    if (!connected && atk.type == AttackType::Charged)
        AddText("whiff", player.x, player.y - 52.0f, {150, 150, 160, 200});
}

// -----------------------------------------------------------------------------
//  Interaction
// -----------------------------------------------------------------------------

void World::ResolveInteractTarget(const GameContext& ctx) {
    (void)ctx;
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
        consider(InteractTarget::Npc, static_cast<int>(i),
                 "Talk to " + npcs[i]->Name(), npcs[i]->x, npcs[i]->y);

    const auto& objects = map.Objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        const MapObject& o = objects[i];
        string label;

        if (o.type == "chest") {
            label = Flagged(o.id) ? "" : "Open chest";
        } else if (o.type == "note") {
            label = "Read note";
        } else if (o.type == "board") {
            label = o.title.empty() ? "Read mission board" : ("Read " + o.title);
        } else if (o.type == "sign") {
            label = "Read sign";
        } else if (o.type == "range" || o.type == "workbench") {
            // Map titles are written as names ("Kitchen fire", "Anvil"), but
            // here they follow "the" mid-sentence.
            string noun = o.title.empty() ? string(o.type == "range" ? "fire" : o.station)
                                          : o.title;
            noun[0] = static_cast<char>(tolower(static_cast<unsigned char>(noun[0])));
            label = (o.type == "range" ? "Cook at the " : "Use the ") + noun;
        } else if (!o.skill.empty()) {
            const int s = SkillFromName(o.skill);
            if (s >= 0 && player.skills.Level(s) < o.skill_level)
                label = "Needs " + o.skill + " " + std::to_string(o.skill_level) +
                        (o.title.empty() ? string("") : " for the " + o.title);
            else
                label = (o.skill == "Mining" ? "Mine " : "Chop ") +
                        (o.title.empty() ? string("node") : o.title);
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
            if (p->danger_level > player.skills.CombatLevel())
                label += "  -  dangerous: Combat " + std::to_string(p->danger_level) + " advised";
            consider(InteractTarget::PortalDoor, 0, label, cx, cy);
        }

    player.interact = best;
}

void World::TryInteract(const GameContext& ctx) {
    if (player.IsDead() || transition_pending) return;

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

            if (o.type == "chest") {
                if (Flagged(o.id)) break;
                SetFlag(o.id);
                Audio::Play(Sfx::ChestOpen);
                if (!o.loot_table.empty()) SpawnLoot(o.loot_table, o.x, o.y + 10.0f, ctx);
                AddText("Opened!", o.x, o.y - 34.0f, {255, 225, 120, 255});
                if (ctx.quests) {
                    QuestEvent e;
                    e.type = ObjectiveType::Interact;
                    e.target = o.id;
                    ctx.quests->Notify(e, player.inventory);
                }
            } else if (o.type == "range") {
                CookOne(o, ctx);
            } else if (o.type == "workbench") {
                WorldRequest r;
                r.type  = WorldRequest::Type::Craft;
                r.id    = o.id;
                r.text  = o.station;
                r.title = o.title.empty() ? (o.station == "anvil" ? "Anvil" : "Workbench") : o.title;
                requests.push_back(r);
            } else if (o.type == "note" || o.type == "sign") {
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
            } else if (!o.skill.empty()) {
                const int s = SkillFromName(o.skill);
                if (s < 0) break;
                if (player.skills.Level(s) < o.skill_level) {
                    AddText("Level too low", o.x, o.y - 34.0f, {255, 140, 140, 255});
                    Audio::Play(Sfx::UiError);
                    break;
                }
                gather_index  = t.index;
                gather_timer  = 0.0f;
                // Higher levels work faster, down to a floor.
                const float speed = 1.0f + 0.02f * player.skills.Level(s);
                gather_needed = std::max(0.9f, o.gather_time / speed);
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
            RequestTransition(p->target_map, p->target_spawn);
            Audio::Play(Sfx::Door);
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

void World::UpdateGathering(float dt, const GameContext& ctx) {
    if (gather_index < 0) return;

    const auto& objects = map.Objects();
    if (gather_index >= static_cast<int>(objects.size())) { gather_index = -1; return; }
    const MapObject& o = objects[gather_index];

    // Walking away cancels it.
    if (Length(o.x - player.x, o.y - player.y) > INTERACT_RANGE + 12.0f) {
        gather_index = -1;
        return;
    }

    // A strike every so often while the work goes on, not just at the end.
    constexpr float STRIKE = 0.62f;
    const float before = gather_timer;
    gather_timer += dt;
    if (std::floor(before / STRIKE) != std::floor(gather_timer / STRIKE) || before == 0.0f)
        Audio::PlayAt(o.skill == "Mining" ? Sfx::Mine : Sfx::Chop, o.x, o.y);
    if (gather_timer < gather_needed) return;

    const int skill = SkillFromName(o.skill);
    if (skill >= 0 && o.yield_xp > 0) player.GrantXp(skill, o.yield_xp);

    if (!o.yield.empty()) {
        if (player.inventory.Add(o.yield, 1) > 0) {
            const ItemDef* d = ctx.items ? ctx.items->Get(o.yield) : nullptr;
            AddText("+ " + (d ? d->name : o.yield), player.x, player.y - 54.0f,
                    {200, 255, 200, 255});
            if (ctx.quests) ctx.quests->RefreshCollectObjectives(player.inventory);
        } else {
            AddText("Inventory full", player.x, player.y - 54.0f, {255, 160, 160, 255});
            gather_index = -1;
            return;
        }
    }

    // Keep going until the player moves or presses the button again.
    gather_timer = 0.0f;
}

float World::GatherProgress() const {
    if (gather_index < 0 || gather_needed <= 0.0f) return 0.0f;
    return std::clamp(gather_timer / gather_needed, 0.0f, 1.0f);
}

// -----------------------------------------------------------------------------
//  Loot and floating text
// -----------------------------------------------------------------------------

// One place where a hit lands, whether it came from a sword, an arrow or a
// bolt of fire, so the element matchup and the XP are applied consistently.
void World::HitEnemy(Enemy& e, const CombatProfile& owner, AttackStyle style,
                     Element element, float damage_mult, float knockback,
                     float from_x, float from_y, const GameContext& ctx) {
    DamageResult r = RollAttack(owner, e.Profile(), style, damage_mult, *ctx.rng);

    // Every way the player can hurt something -- swing, arrow, bolt, burning
    // ground -- comes through here, so this is where the bar first appears.
    // Before the miss check: a swing that misses has still started the fight.
    e.RevealHealthBar();

    if (!r.hit) {
        AddText("miss", e.x, e.y - 46.0f, {150, 150, 168, 235});
        return;
    }

    // Elements only matter when both sides have one.
    const float matchup = ElementMultiplier(element, e.ElementOf());
    int damage = static_cast<int>(roundf(r.damage * matchup));
    if (r.damage > 0 && damage <= 0) damage = 1;

    if (damage <= 0) {
        AddText("0", e.x, e.y - 46.0f, {120, 160, 220, 255});
        Audio::PlayAt(Sfx::Block, e.x, e.y);
        return;
    }
    // A killing blow is heard as the death, not as a hit on top of it.
    if (damage < e.hp)
        Audio::PlayAt(r.max_hit ? Sfx::HitCrit : Sfx::Hit, e.x, e.y);

    e.Damage(damage);
    player.AwardCombatXp(damage, AttackType::Light);

    SDL_Color color = r.max_hit ? SDL_Color{255, 220, 90, 255}
                                : SDL_Color{255, 245, 235, 255};
    string label = std::to_string(damage);
    if (matchup > 1.05f) {
        color = ElementColor(element);
        label += "!";                      // strong against this creature
    } else if (matchup < 0.95f) {
        color = {150, 150, 170, 255};      // resisted
    }
    AddText(label, e.x, e.y - 46.0f, color);

    const float dx = e.x - from_x, dy = e.y - from_y;
    const float len = std::max(1.0f, Length(dx, dy));
    e.knock_x += (dx / len) * knockback;
    e.knock_y += (dy / len) * knockback;
}

void World::SpawnProjectile(const string& def_id, float x, float y,
                            float dir_x, float dir_y,
                            const CombatProfile& owner, AttackStyle style,
                            float damage_mult, bool from_player,
                            const GameContext& ctx) {
    const ProjectileDef* def = ctx.projectiles ? ctx.projectiles->Get(def_id) : nullptr;
    if (!def) {
        SDL_Log("World: unknown projectile '%s'", def_id.c_str());
        return;
    }

    const float len = Length(dir_x, dir_y);
    if (len < 0.001f) return;

    Projectile p;
    p.def = def;
    p.x = x;
    p.y = y;
    p.vx = (dir_x / len) * def->speed;
    p.vy = (dir_y / len) * def->speed;
    p.angle = atan2f(p.vy, p.vx);
    p.life = def->life;
    p.owner = owner;
    p.style = style;
    p.element = def->element;
    p.damage_mult = damage_mult;
    p.from_player = from_player;
    p.pierce_left  = def->pierce;
    p.bounces_left = def->bounces;
    projectiles.push_back(p);
}

void World::AddGroundEffect(const GroundEffect& effect) {
    ground_effects.push_back(effect);
}

void World::UpdateProjectiles(float dt, const GameContext& ctx) {
    for (Projectile& p : projectiles) {
        if (p.finished || !p.def) continue;

        p.life -= dt;
        if (p.life <= 0.0f) { p.finished = true; }

        // Step in slices no longer than half the projectile's own radius, so a
        // fast arrow cannot pass through a wall or a thin target between one
        // frame and the next. Radius rather than a fixed distance: a small
        // fast bolt needs finer steps than a large slow one.
        const float travel = Length(p.vx, p.vy) * dt;
        const float max_step = std::max(2.0f, p.def->radius * 0.5f);
        const int steps = std::clamp(static_cast<int>(travel / max_step) + 1, 1, 32);
        const float step_dt = dt / steps;

        for (int i = 0; i < steps && !p.finished; ++i) {
            const float dx = p.vx * step_dt;
            const float dy = p.vy * step_dt;

            // Walls: resolve to the surface rather than stopping wherever the
            // step happened to land, so an impact is drawn on the wall and a
            // fire patch burns in front of it instead of inside it.
            const Map::Contact c = map.SweepPoint(p.x, p.y, dx, dy, p.def->radius);
            if (c.hit) {
                p.x = c.x;
                p.y = c.y;

                const bool can_bounce = p.bounces_left > 0 &&
                                        (c.nx != 0.0f || c.ny != 0.0f);
                AddImpact(p, c.nx, c.ny);
                Audio::PlayAt(Sfx::Impact, p.x, p.y, 0.7f);

                if (!can_bounce) {
                    p.hit_wall = true;
                    p.finished = true;
                    break;
                }

                --p.bounces_left;
                // Reflect about the surface: v' = v - 2(v.n)n.
                const float vn = p.vx * c.nx + p.vy * c.ny;
                p.vx -= 2.0f * vn * c.nx;
                p.vy -= 2.0f * vn * c.ny;

                const float keep = 1.0f - std::clamp(p.def->bounce_damping, 0.0f, 1.0f);
                p.vx *= keep;
                p.vy *= keep;
                p.angle = atan2f(p.vy, p.vx);

                // Ease off the surface so the next step does not immediately
                // find the same wall it just left.
                p.x += c.nx * (p.def->radius * 0.5f + 0.5f);
                p.y += c.ny * (p.def->radius * 0.5f + 0.5f);

                // A bounce that has lost almost all its speed is spent.
                if (Length(p.vx, p.vy) < 40.0f) { p.finished = true; break; }
                continue;
            }

            p.x += dx;
            p.y += dy;
            if (p.def->spin) p.spin_angle += 14.0f * step_dt;

            const SDL_FRect box = {p.x - p.def->radius, p.y - p.def->radius,
                                   p.def->radius * 2, p.def->radius * 2};

            if (p.from_player) {
                for (auto& e : enemies) {
                    if (p.finished) break;
                    if (e->Dead() || e->CurrentState() == Enemy::State::Dead) continue;
                    if (!RectsOverlap(box, e->BodyBox())) continue;

                    const void* key = e.get();
                    if (std::find(p.already_hit.begin(), p.already_hit.end(), key) !=
                        p.already_hit.end())
                        continue;
                    p.already_hit.push_back(key);

                    HitEnemy(*e, p.owner, p.style, p.element, p.damage_mult,
                             p.def->knockback, p.x - p.vx, p.y - p.vy, ctx);

                    if (p.pierce_left > 0) --p.pierce_left;
                    else                    p.finished = true;
                }
            } else if (!player.IsDead() &&
                       RectsOverlap(box, player.BodyBox())) {
                DamageResult r = RollAttack(p.owner, player.Profile(), p.style,
                                            p.damage_mult, *ctx.rng);
                if (r.hit && r.damage > 0) {
                    player.Damage(r.damage);
                    player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
                    player.GrantXp(SKILL_DEFENCE, std::max(1, r.damage));
                    AddText(std::to_string(r.damage), player.x, player.y - 44.0f,
                            {235, 70, 70, 255});
                } else {
                    AddText("miss", player.x, player.y - 44.0f, {150, 150, 168, 235});
                }
                p.finished = true;
            }
        }

        // What it leaves behind when it stops. Against a wall the contact
        // point is flush with the surface, so nudge the effect back along the
        // direction of travel -- burning ground should lie in front of the
        // wall where someone can be standing in it, not half inside it.
        if (p.finished) {
            float ex = p.x, ey = p.y;
            if (p.hit_wall) {
                const float len = Length(p.vx, p.vy);
                if (len > 0.0f) {
                    const float back = std::max(p.def->patch_radius,
                                                p.def->erupt_radius) * 0.5f + 2.0f;
                    ex -= p.vx / len * back;
                    ey -= p.vy / len * back;
                }
            }
            if (p.def->patch_time > 0.0f) {
                GroundEffect g;
                g.x = ex;
                g.y = ey;
                g.radius = p.def->patch_radius;
                g.life = g.max_life = p.def->patch_time;
                g.tick_interval = p.def->patch_tick;
                g.tick_timer = 0.0f;
                g.damage = p.def->patch_damage;
                g.element = p.def->element;
                g.owner = p.owner;
                g.from_player = p.from_player;
                AddGroundEffect(g);
            }
            if (p.def->erupts) {
                GroundEffect g;
                g.x = ex;
                g.y = ey;
                g.radius = p.def->erupt_radius;
                g.delay = p.def->erupt_delay;
                g.life = g.max_life = p.def->erupt_delay + 0.28f;
                g.damage = p.def->erupt_damage;
                g.element = p.def->element;
                g.owner = p.owner;
                g.from_player = p.from_player;
                g.burst = true;
                AddGroundEffect(g);
            }
        }
    }

    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
                                     [](const Projectile& p) { return p.finished; }),
                      projectiles.end());
}

void World::UpdateElevation() {
    // One pass a frame rather than a lookup inside every draw call: the height
    // grid is a hash and a clamp, but Render is called from a sorted queue that
    // may visit the same entity's bounds several times.
    if (!map.HasElevation()) {
        // A hop on flat ground still leaves the ground.
        player.draw_lift = player.IsJumping() ? player.JumpLift() : 0.0f;
        for (auto& e : enemies) e->draw_lift = 0.0f;
        for (auto& n : npcs)    n->draw_lift = 0.0f;
        return;
    }
    player.draw_lift = player.IsJumping() ? player.JumpLift()
                                          : map.HeightAt(player.x, player.y);
    for (auto& e : enemies) e->draw_lift = map.HeightAt(e->x, e->y);
    for (auto& n : npcs)    n->draw_lift = map.HeightAt(n->x, n->y);
}

void World::AddImpact(const Projectile& p, float nx, float ny) {
    if (!p.def || p.def->impact_size <= 0.0f) return;

    Impact im;
    im.x = p.x;
    im.y = p.y;
    im.nx = nx;
    im.ny = ny;
    im.radius = p.def->impact_size;
    im.life = im.max_life = 0.22f;
    // An elemental bolt splashes in its own colour; an untyped arrow throws
    // dust, so it takes the tint of the projectile art instead.
    im.color = (p.element != Element::None) ? ElementColor(p.element)
                                            : SDL_Color{214, 200, 176, 255};
    impacts.push_back(im);
}

void World::UpdateImpacts(float dt) {
    for (Impact& im : impacts) {
        im.life -= dt;
        if (im.life <= 0.0f) im.finished = true;
    }
    impacts.erase(std::remove_if(impacts.begin(), impacts.end(),
                                 [](const Impact& i) { return i.finished; }),
                  impacts.end());
}

void World::UpdateGroundEffects(float dt, const GameContext& ctx) {
    for (GroundEffect& g : ground_effects) {
        if (g.finished) continue;

        if (g.delay > 0.0f) {
            g.delay -= dt;
            if (g.delay > 0.0f) continue;
        }

        g.life -= dt;
        if (g.life <= 0.0f) g.finished = true;

        bool apply = false;
        if (g.burst) {
            // An eruption hits once, the moment it goes off.
            apply = true;
            g.burst = false;
            g.finished = false;
        } else {
            g.tick_timer -= dt;
            if (g.tick_timer <= 0.0f) {
                g.tick_timer += g.tick_interval;
                apply = true;
            }
        }
        if (!apply) continue;

        const SDL_FRect area = {g.x - g.radius, g.y - g.radius,
                                g.radius * 2, g.radius * 2};

        if (g.from_player) {
            for (auto& e : enemies) {
                if (e->Dead() || e->CurrentState() == Enemy::State::Dead) continue;
                if (!RectsOverlap(area, e->BodyBox())) continue;
                HitEnemy(*e, g.owner, AttackStyle::Magic, g.element,
                         static_cast<float>(g.damage) * 0.5f, 8.0f, g.x, g.y, ctx);
            }
        } else if (!player.IsDead() && RectsOverlap(area, player.BodyBox())) {
            player.Damage(std::max(1, g.damage));
            player.skills.SetCurrent(SKILL_HITPOINTS, player.hp);
            AddText(std::to_string(g.damage), player.x, player.y - 44.0f,
                    {235, 90, 70, 255});
        }
    }

    ground_effects.erase(std::remove_if(ground_effects.begin(), ground_effects.end(),
                                        [](const GroundEffect& g) { return g.finished; }),
                         ground_effects.end());
}

void World::SpawnLoot(const string& table_id, float x, float y, const GameContext& ctx) {
    if (!ctx.loot) return;
    vector<LootDrop> drops = ctx.loot->Roll(table_id);

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
                     const GameContext& ctx) {
    if (item_id.empty() || qty <= 0) return;
    Pickup p;
    p.item_id = item_id;
    p.qty     = qty;
    p.x = x;
    p.y = y;
    if (const ItemDef* d = ctx.items ? ctx.items->Get(item_id) : nullptr) p.icon = d->icon;
    pickups.push_back(p);
}

void World::UpdatePickups(float dt, const GameContext& ctx) {
    for (auto& p : pickups) {
        p.life += dt;
        p.bob  += dt * 3.4f;

        if (p.collected || p.life < PICKUP_ARM) continue;
        if (player.IsDead()) continue;
        if (Length(p.x - player.x, p.y - player.y) > PICKUP_RANGE) continue;

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
    t.y = y;
    t.life = t.max_life = life;
    t.color = color;
    texts.push_back(t);
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

// -----------------------------------------------------------------------------
//  Rendering
// -----------------------------------------------------------------------------

void World::Render(SDL_Renderer* r, TextureCache& cache) const {
    const SDL_Color bg = map.BackgroundColor();
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(r);

    map.RenderLayer(r, cache, camera, LAYER_GROUND);
    // The exposed earth on the downhill side of every raised cell, drawn over
    // the ground and under everything that stands on it.
    map.RenderCliffs(r, cache, camera);

    // Burning ground and pending eruptions lie on the floor, under everyone.
    // Drawn as a squashed disc rather than a rectangle: a hard-edged box reads
    // as a UI element, and this is meant to look like something on the grass.
    auto fill_disc = [&](float cx, float cy, float rx, float ry, SDL_Color c) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        const int rows = std::max(3, static_cast<int>(ry * 2));
        for (int i = 0; i < rows; ++i) {
            // Half-width of the disc at this height.
            const float t = (i + 0.5f) / rows * 2.0f - 1.0f;
            const float half = rx * sqrtf(std::max(0.0f, 1.0f - t * t));
            const SDL_FRect span = {cx - half, cy + t * ry, half * 2.0f, ry * 2.0f / rows + 1.0f};
            SDL_RenderFillRect(r, &span);
        }
    };

    for (const GroundEffect& g : ground_effects) {
        const SDL_FPoint centre = camera.ToScreen(g.x, g.y);
        const float rx = g.radius * camera.zoom;
        const float ry = g.radius * 0.55f * camera.zoom;
        const SDL_Color c = ElementColor(g.element);

        if (g.delay > 0.0f) {
            // Telegraph the eruption: an outline that tightens as it arms.
            const float t = 1.0f - std::clamp(g.delay / 0.5f, 0.0f, 1.0f);
            fill_disc(centre.x, centre.y, rx * (0.55f + 0.45f * t), ry * (0.55f + 0.45f * t),
                      {c.r, c.g, c.b, static_cast<Uint8>(40 + 90 * t)});
        } else {
            const float t = std::clamp(g.life / std::max(0.01f, g.max_life), 0.0f, 1.0f);
            // A brighter core inside a wider glow.
            fill_disc(centre.x, centre.y, rx, ry,
                      {c.r, c.g, c.b, static_cast<Uint8>(70 * t)});
            fill_disc(centre.x, centre.y, rx * 0.6f, ry * 0.6f,
                      {c.r, c.g, c.b, static_cast<Uint8>(120 * t)});
        }
    }

    // Everything at ground level draws in baseline order, so the player walks
    // behind a tree trunk and in front of the grass at its foot.
    struct Item { float sort_y; int kind; const void* ptr; };
    vector<Item> queue;

    vector<const TileInstance*> decor;
    map.CollectDecor(camera, decor);
    queue.reserve(decor.size() + enemies.size() + npcs.size() + pickups.size() + 8);
    for (const TileInstance* t : decor) queue.push_back({t->sort_y, 0, t});

    const SDL_FRect view = camera.VisibleWorldRect(96.0f);

    for (const auto& o : map.Objects()) {
        if (o.sprite.empty()) continue;
        if (o.x < view.x || o.x > view.x + view.w ||
            o.y < view.y || o.y > view.y + view.h) continue;
        queue.push_back({o.y, 3, &o});
    }
    for (const auto& p : pickups) {
        if (!RectsOverlap(p.Bounds(), view)) continue;
        queue.push_back({p.y, 2, &p});
    }
    for (const auto& p : projectiles) {
        if (!RectsOverlap(p.Bounds(), view)) continue;
        queue.push_back({p.y, 4, &p});
    }
    for (const auto& e : enemies) {
        if (e->CorpseGone()) continue;      // despawned, waiting to respawn
        if (!RectsOverlap(e->BodyBox(), view)) continue;
        queue.push_back({e->SortY(), 1, e.get()});
    }
    for (const auto& n : npcs) {
        if (!RectsOverlap(n->BodyBox(), view)) continue;
        queue.push_back({n->SortY(), 1, n.get()});
    }
    if (!player.IsDead() || player.DeathTimer() > 0.0f)
        queue.push_back({player.SortY(), 1, &player});

    std::stable_sort(queue.begin(), queue.end(),
                     [](const Item& a, const Item& b) { return a.sort_y < b.sort_y; });

    // Everything a piece of scenery must not be allowed to hide.
    struct Combatant { SDL_FRect box; float sort_y; };
    vector<Combatant> combatants;
    // Body boxes, not sprite frames: a 64px frame is mostly empty around a
    // figure twenty pixels wide.
    combatants.push_back({player.BodyBox(), player.SortY()});
    for (const auto& e : enemies) {
        if (e->CurrentState() == Enemy::State::Dead) continue;
        if (!RectsOverlap(e->BodyBox(), view)) continue;
        // Only what the player is actually fighting, or is about to. Every
        // grazing deer and hare used to count, so trees all over the
        // greenwood went see-through and back as the animals wandered behind
        // them, which read as a rendering fault rather than as help.
        const bool close = Length(e->x - player.x, e->y - player.y) < 140.0f;
        if (!e->Engaged() && !close) continue;
        combatants.push_back({e->BodyBox(), e->SortY()});
    }

    // True when this scenery is tall enough to swallow someone and is drawn
    // over one of them. Measured against the pixels the art actually draws:
    // tree images sit on canvases several times wider than the tree, and
    // testing the canvas faded a tree whenever the player walked past a
    // hundred pixels to one side of it.
    auto covers_someone = [&](const SDL_FRect& canvas, float sort_y, const string& path) {
        if (canvas.h <= 48.0f) return false;
        const SDL_FRect f = path.empty() ? SDL_FRect{0.0f, 0.0f, 1.0f, 1.0f}
                                         : cache.OpaqueBounds(path);
        const SDL_FRect art = {canvas.x + f.x * canvas.w, canvas.y + f.y * canvas.h,
                               f.w * canvas.w, f.h * canvas.h};
        for (const Combatant& c : combatants)
            if (sort_y > c.sort_y && RectsOverlap(art, c.box)) return true;
        return false;
    };

    for (const Item& it : queue) {
        switch (it.kind) {
            case 0: {
                const TileInstance* t = static_cast<const TileInstance*>(it.ptr);
                // Decor tiles share the map texture list, so draw through the
                // map to keep that indirection in one place.
                map.RenderTile(r, cache, camera, *t,
                               covers_someone(t->rect, t->sort_y, map.TexturePath(*t)) ? 110 : 255);
                break;
            }
            case 1: {
                const Entity* e = static_cast<const Entity*>(it.ptr);
                e->Render(r, cache, camera);
                break;
            }
            case 2: {
                const Pickup* p = static_cast<const Pickup*>(it.ptr);
                const float bob = sinf(p->bob) * 2.0f;
                SDL_Texture* tex = p->icon.empty() ? nullptr : cache.Get(p->icon);
                SDL_FRect world = {p->x - 8.0f, p->y - 14.0f + bob, 16.0f, 16.0f};
                SDL_FRect dst = camera.ToScreenRect(world);
                if (tex) {
                    SDL_RenderTexture(r, tex, nullptr, &dst);
                } else {
                    SDL_SetRenderDrawColor(r, 240, 205, 90, 235);
                    SDL_RenderFillRect(r, &dst);
                    SDL_SetRenderDrawColor(r, 90, 70, 20, 255);
                    SDL_RenderRect(r, &dst);
                }
                break;
            }
            case 4: {
                const Projectile* p = static_cast<const Projectile*>(it.ptr);
                if (!p->def) break;
                SDL_Texture* tex = cache.Get(p->def->sprite);

                // Draw at the art's own proportions. An arrow is long and thin;
                // forcing it into a square makes it look like a thrown brick.
                float aw = 16.0f, ah = 16.0f;
                if (tex) {
                    float tw = 0, th = 0;
                    SDL_GetTextureSize(tex, &tw, &th);
                    if (tw > 0 && th > 0) { aw = tw; ah = th; }
                }
                aw *= p->def->scale;
                ah *= p->def->scale;

                const SDL_FRect world = {p->x - aw / 2.0f, p->y - ah / 2.0f, aw, ah};
                const SDL_FRect dst = camera.ToScreenRect(world);

                if (tex) {
                    // One sprite covers every direction: it is drawn turned to
                    // face the way it is travelling.
                    const double deg = p->def->spin
                        ? p->spin_angle * 57.2957795
                        : p->angle * 57.2957795 + p->def->sprite_angle;
                    SDL_SetTextureColorMod(tex, p->def->tint.r, p->def->tint.g, p->def->tint.b);
                    SDL_RenderTextureRotated(r, tex, nullptr, &dst, deg, nullptr, SDL_FLIP_NONE);
                    SDL_SetTextureColorMod(tex, 255, 255, 255);
                } else {
                    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(r, p->def->tint.r, p->def->tint.g,
                                           p->def->tint.b, 235);
                    SDL_RenderFillRect(r, &dst);
                }
                break;
            }
            case 3: {
                const MapObject* o = static_cast<const MapObject*>(it.ptr);
                const bool used = Flagged(o->id) && !o->sprite_open.empty();
                SDL_Texture* tex = cache.Get(used ? o->sprite_open : o->sprite);
                if (!tex) break;
                float tw = 0, th = 0;
                SDL_GetTextureSize(tex, &tw, &th);
                // Objects stand on their position, like characters do.
                const SDL_FRect world = {o->x - tw / 2.0f, o->y - th, tw, th};
                const SDL_FRect dst = camera.ToScreenRect(world);

                // Tall scenery drawn in front of someone goes translucent
                // while it overlaps them, so nobody fights behind a bush.
                const Uint8 alpha = covers_someone(world, o->y, used ? o->sprite_open : o->sprite)
                                        ? 110 : 255;

                SDL_SetTextureAlphaMod(tex, alpha);
                SDL_RenderTexture(r, tex, nullptr, &dst);
                SDL_SetTextureAlphaMod(tex, 255);
                break;
            }
        }
    }

    // Impact marks last a fifth of a second and are drawn over everything at
    // ground level, because the point of them is to be noticed: without one, a
    // bolt that hits a wall simply stops existing and it is not obvious whether
    // it was blocked or ran out of range.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const Impact& im : impacts) {
        const float t = std::clamp(im.life / std::max(0.01f, im.max_life), 0.0f, 1.0f);
        const SDL_FPoint centre = camera.ToScreen(im.x, im.y);

        // A flash that opens outwards as it fades.
        const float rad = im.radius * camera.zoom * (1.0f + (1.0f - t) * 1.4f);
        fill_disc(centre.x, centre.y, rad, rad * 0.75f,
                  {im.color.r, im.color.g, im.color.b,
                   static_cast<Uint8>(190 * t)});

        // Three shards thrown back off the face it struck. Fixed rather than
        // random: a spray that reshuffles every frame reads as noise.
        if (im.nx != 0.0f || im.ny != 0.0f) {
            const float px = -im.ny, py = im.nx;      // along the surface
            const float reach = im.radius * (2.0f + (1.0f - t) * 3.0f) * camera.zoom;
            SDL_SetRenderDrawColor(r, im.color.r, im.color.g, im.color.b,
                                   static_cast<Uint8>(220 * t));
            for (float spread : {-0.6f, 0.0f, 0.6f}) {
                const float dx = im.nx + px * spread;
                const float dy = im.ny + py * spread;
                SDL_RenderLine(r, centre.x, centre.y,
                               centre.x + dx * reach, centre.y + dy * reach);
            }
        }
    }

    map.RenderLayer(r, cache, camera, LAYER_OVERHEAD);

    // Leaves, fireflies and dust, and the vignette -- over the world, under
    // the bars and the HUD.
    ambience.Render(r, camera);

    // Health bars over anything the player has attacked. Last, above canopy
    // and roofs, because a bar hidden behind a tree is no use.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const auto& e : enemies) {
        if (!e->HealthBarVisible()) continue;
        const SDL_FRect body = e->BodyBox();
        if (!RectsOverlap(body, view)) continue;

        // Sized to the creature and hung just over its body box -- a hare's
        // bar sits low and narrow, an orc's high and wide -- and lifted with
        // the ground it stands on, like the sprite.
        const float w = std::max(20.0f, body.w + 4.0f);
        const SDL_FRect s = camera.ToScreenRect({e->x - w / 2.0f, body.y - e->draw_lift - 5.0f,
                                                 w, 3.0f});

        // Whole screen pixels, so the proportions drawn are the true ones and
        // not whatever sub-pixel scaling makes of them.
        const float bx = roundf(s.x), by = roundf(s.y);
        const int   bw = std::max(8, static_cast<int>(roundf(s.w)));
        const int   bh = std::max(5, static_cast<int>(roundf(s.h)));
        const int   inner = bw - 2;

        const int fill  = HealthBarFillPixels(e->hp, e->max_hp, inner);
        const int trail = std::clamp(static_cast<int>(std::lround(inner * e->HealthTrail())),
                                     fill, inner);

        SDL_SetRenderDrawColor(r, 14, 10, 8, 230);
        const SDL_FRect back = {bx, by, static_cast<float>(bw), static_cast<float>(bh)};
        SDL_RenderFillRect(r, &back);

        if (trail > fill) {
            SDL_SetRenderDrawColor(r, 240, 214, 160, 240);
            const SDL_FRect band = {bx + 1.0f + fill, by + 1.0f,
                                    static_cast<float>(trail - fill), bh - 2.0f};
            SDL_RenderFillRect(r, &band);
        }
        if (fill > 0) {
            SDL_SetRenderDrawColor(r, 196, 44, 40, 255);
            const SDL_FRect red = {bx + 1.0f, by + 1.0f, static_cast<float>(fill), bh - 2.0f};
            SDL_RenderFillRect(r, &red);
            // A lighter top row so it reads as a bar rather than a smear.
            SDL_SetRenderDrawColor(r, 236, 96, 84, 255);
            const SDL_FRect shine = {bx + 1.0f, by + 1.0f, static_cast<float>(fill), 1.0f};
            SDL_RenderFillRect(r, &shine);
        }
    }
}
