#include "shop.h"
#include <fstream>

bool ShopDatabase::Load(const string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("ShopDatabase: cannot open '%s'", path.c_str());
        return false;
    }
    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        SDL_Log("ShopDatabase: bad JSON in '%s': %s", path.c_str(), e.what());
        return false;
    }

    defs.clear();
    const json& shops = root.contains("shops") ? root["shops"] : root;
    for (auto it = shops.begin(); it != shops.end(); ++it) {
        if (!it.value().is_object()) continue;
        const json& o = it.value();
        ShopDef s;
        s.id     = it.key();
        s.name   = o.value("name", s.id);
        s.type   = o.value("type", string("general"));
        s.keeper = o.value("keeper", string(""));
        s.town   = o.value("town", string(""));
        s.markup = std::max(1.0f, o.value("markup", 1.0f));
        if (o.contains("buys"))
            for (auto b = o["buys"].begin(); b != o["buys"].end(); ++b)
                s.buys[b.key()] = std::clamp(b.value().get<float>(), 0.0f, 0.95f);
        if (o.contains("sells"))
            for (const json& line : o["sells"]) {
                ShopStock st;
                st.item  = line.value("item", string(""));
                st.stock = std::max(1, line.value("stock", 1));
                if (line.contains("after")) {
                    if (line["after"].is_string()) st.after.push_back(line["after"].get<string>());
                    else for (const json& q : line["after"]) st.after.push_back(q.get<string>());
                }
                s.sells.push_back(st);
            }
        defs[s.id] = s;
    }
    SDL_Log("ShopDatabase: loaded %d shops (%s)", static_cast<int>(defs.size()), path.c_str());
    return true;
}

const ShopDef* ShopDatabase::Get(const string& id) const {
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}

// --- the ledger -----------------------------------------------------------------

void ShopLedger::SetDay(int d) {
    if (d == day) return;
    day = d;
    sold.clear();
}

int ShopLedger::Remaining(const ShopDef& shop, const string& item) const {
    int stock = 0;
    for (const ShopStock& line : shop.sells)
        if (line.item == item) stock += line.stock;
    auto s = sold.find(shop.id);
    if (s != sold.end()) {
        auto i = s->second.find(item);
        if (i != s->second.end()) stock -= i->second;
    }
    return std::max(0, stock);
}

void ShopLedger::Record(const string& shop, const string& item, int qty) {
    if (qty <= 0) return;
    sold[shop][item] += qty;
    if (journal) sales.push_back({shop, item, qty});
}

json ShopLedger::ToJson() const {
    json j;
    j["day"] = day;
    j["sold"] = json::object();
    for (const auto& s : sold)
        for (const auto& i : s.second)
            if (i.second > 0) j["sold"][s.first][i.first] = i.second;
    return j;
}

void ShopLedger::FromJson(const json& j) {
    Clear();
    if (!j.is_object()) return;
    day = j.value("day", -1);
    if (j.contains("sold") && j["sold"].is_object())
        for (auto s = j["sold"].begin(); s != j["sold"].end(); ++s)
            for (auto i = s.value().begin(); i != s.value().end(); ++i)
                if (i.value().is_number_integer() && i.value().get<int>() > 0)
                    sold[s.key()][i.key()] = i.value().get<int>();
}

// --- prices -----------------------------------------------------------------------

namespace Trade {

vector<string> Tags(const ItemDatabase& db, const ItemDef& d) {
    vector<string> t = d.tags;
    auto add = [&](const string& tag) {
        if (std::find(t.begin(), t.end(), tag) == t.end()) t.push_back(tag);
    };

    if (!d.piece.empty()) add(d.piece);
    if (!d.tier.empty())  add("tier:" + d.tier);
    if (d.metal) add("metal");

    switch (d.slot) {
        case SLOT_WEAPON:
            add("weapon");
            add(d.kind == WeaponKind::Bow ? "bow" : d.kind == WeaponKind::Staff ? "staff" : "melee");
            break;
        case SLOT_SHIELD: case SLOT_HEAD: case SLOT_BODY:
        case SLOT_HANDS:  case SLOT_LEGS: case SLOT_FEET:
            add("armour");
            break;
        case SLOT_AMULET: case SLOT_RING:
            add("jewellery");
            break;
        default: break;
    }

    if (!d.tool.empty()) { add("tool"); add(d.tool); }
    if (!d.cook_result.empty()) add("raw");
    if (d.heal > 0) add("food");
    if (d.fish_level > 0) add("fish");
    else if (d.heal > 0)
        // Cooked fish are still fish to a fishmonger.
        for (const auto& kv : db.All())
            if (kv.second.fish_level > 0 && kv.second.cook_result == d.id) { add("fish"); break; }
    return t;
}

bool Tradeable(const ItemDef& d) {
    return d.id != "coins" && d.value > 1;
}

int BuyPrice(const ShopDef& shop, const ItemDef& d) {
    return std::max(d.value, static_cast<int>(std::ceil(d.value * shop.markup - 1e-4f)));
}

float SellRate(const ShopDef& shop, const ItemDatabase& db, const ItemDef& d) {
    if (!Tradeable(d)) return 0.0f;
    float best = 0.0f;
    auto any = shop.buys.find("*");
    if (any != shop.buys.end()) best = any->second;
    for (const string& tag : Tags(db, d)) {
        auto it = shop.buys.find(tag);
        if (it != shop.buys.end()) best = std::max(best, it->second);
    }
    return best;
}

int SellPrice(const ShopDef& shop, const ItemDatabase& db, const ItemDef& d) {
    const float rate = SellRate(shop, db, d);
    if (rate <= 0.0f) return 0;
    // Never nothing for something the shop takes, and never as much as the
    // cheapest a shop could sell it for.
    return std::clamp(static_cast<int>(d.value * rate), 1, d.value - 1);
}

bool OnShelf(const ShopStock& line, const QuestLog* quests) {
    for (const string& q : line.after)
        if (!quests || !quests->IsComplete(q)) return false;
    return true;
}

vector<const ShopStock*> Shelf(const ShopDef& shop, const QuestLog* quests) {
    vector<const ShopStock*> out;
    for (const ShopStock& line : shop.sells)
        if (OnShelf(line, quests)) out.push_back(&line);
    return out;
}

TradeOutcome Buy(const ShopDef& shop, ShopLedger& ledger, Inventory& bag,
                 const ItemDatabase& db, const QuestLog* quests,
                 const string& item, int qty) {
    TradeOutcome out;
    const ItemDef* d = db.Get(item);
    const ShopStock* line = nullptr;
    for (const ShopStock& l : shop.sells) if (l.item == item) { line = &l; break; }
    if (!d || !line || qty <= 0) return out;
    if (!OnShelf(*line, quests)) { out.result = TradeResult::Locked; return out; }

    int n = std::min(qty, ledger.Remaining(shop, item));
    if (n <= 0) { out.result = TradeResult::SoldOut; return out; }

    const int price = BuyPrice(shop, *d);
    n = std::min(n, bag.Coins() / price);
    if (n <= 0) { out.result = TradeResult::NoCoins; return out; }

    // Room: a stack needs one slot, or none if there is one already; anything
    // else takes a slot each. Paying can empty the coin slot, which counts.
    const bool coins_freed = bag.Coins() == n * price;
    const int free = bag.FreeSlots() + (coins_freed ? 1 : 0);
    if (d->stackable) { if (bag.Count(item) == 0 && free == 0) n = 0; }
    else n = std::min(n, free);
    if (n <= 0) { out.result = TradeResult::BagFull; return out; }

    bag.SpendCoins(n * price);
    const int added = bag.Add(item, n);
    if (added < n) bag.AddCoins((n - added) * price);   // should not happen; never charge for nothing
    if (added <= 0) { out.result = TradeResult::BagFull; return out; }

    ledger.Record(shop.id, item, added);
    out.result = TradeResult::Ok;
    out.qty = added;
    out.coins = added * price;
    return out;
}

TradeOutcome Sell(const ShopDef& shop, Inventory& bag, const ItemDatabase& db,
                  const string& item, int qty) {
    TradeOutcome out;
    const ItemDef* d = db.Get(item);
    if (!d || qty <= 0) return out;
    const int price = SellPrice(shop, db, *d);
    if (price <= 0) { out.result = TradeResult::WontBuy; return out; }

    const int n = std::min(qty, bag.Count(item));
    if (n <= 0) { out.result = TradeResult::NotHeld; return out; }

    bag.Remove(item, n);
    if (bag.Add("coins", n * price) <= 0) {
        // A full bag with no coin stack and the item still there: undo.
        bag.Add(item, n);
        out.result = TradeResult::BagFull;
        return out;
    }
    out.result = TradeResult::Ok;
    out.qty = n;
    out.coins = n * price;
    return out;
}

const char* Message(TradeResult r) {
    switch (r) {
        case TradeResult::Ok:      return "Done.";
        case TradeResult::Locked:  return "That is not for sale to you yet.";
        case TradeResult::SoldOut: return "Sold out until tomorrow.";
        case TradeResult::NoCoins: return "You cannot afford that.";
        case TradeResult::BagFull: return "Your pack is full.";
        case TradeResult::WontBuy: return "They do not deal in that.";
        case TradeResult::NotHeld: return "You have none of those.";
        default:                   return "Nothing to trade.";
    }
}

} // namespace Trade
