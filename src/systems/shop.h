#pragma once
#include "../headers.h"
#include "items.h"
#include "quest.h"

// -----------------------------------------------------------------------------
//  Traders.
//
//  A shop is data (data/shops.json): who keeps it, what is on the shelf and how
//  much of it, what the keeper charges, and what they will pay for. Nothing
//  about any one shop is in code.
//
//  Prices come from an item's value. A shop charges its markup on the value,
//  never less than the value itself, and pays a fraction of the value for the
//  kinds of thing it deals in: a forge pays well for ore, bars and metalwork, a
//  fishmonger for fish, and a general store takes anything, cheaply. Every rate
//  is below every markup, so nothing can be bought in one shop and sold in
//  another for more than it cost -- the profit is in what the player gathers
//  and makes, and crafting always adds to what the materials were worth (see
//  ItemDatabase::SettleCraftValues).
//
//  Shelves are limited. What a shop sells runs out, and every shop restocks
//  when the quest day turns over at dawn.
// -----------------------------------------------------------------------------

struct ShopStock {
    string item;
    int    stock = 1;               // how many a day
    vector<string> after;           // quests to finish before it is on the shelf
};

struct ShopDef {
    string id, name, type, keeper, town;
    float  markup = 1.0f;           // price = value * markup, never below value
    map<string, float> buys;        // item tag -> fraction of value paid; "*" is anything
    vector<ShopStock> sells;
    bool   General() const { return type == "general"; }
};

class ShopDatabase {
public:
    bool Load(const string& path);
    const ShopDef* Get(const string& id) const;
    const map<string, ShopDef>& All() const { return defs; }

private:
    map<string, ShopDef> defs;
};

// Stock sold to the player today, per shop. Saved with the world.
class ShopLedger {
public:
    // A new day restocks every shop.
    void SetDay(int day);
    int  Day() const { return day; }
    int  Remaining(const ShopDef& shop, const string& item) const;
    void Record(const string& shop, const string& item, int qty);
    void Clear() { sold.clear(); day = -1; }
    // While set, every sale is also written down, for a guest's machine to
    // tell the host what it bought of a shelf everyone shares.
    bool journal = false;
    struct Sale { string shop, item; int qty = 0; };
    vector<Sale> sales;

    json ToJson() const;
    void FromJson(const json& j);

private:
    int day = -1;
    map<string, map<string, int>> sold;   // shop -> item -> sold today
};

enum class TradeResult { Ok, Unknown, Locked, SoldOut, NoCoins, BagFull, WontBuy, NotHeld };

struct TradeOutcome {
    TradeResult result = TradeResult::Unknown;
    int qty = 0;       // how many changed hands
    int coins = 0;     // paid or earned
};

namespace Trade {

// What kind of thing an item is, for deciding who buys it: "ore", "bar",
// "weapon", "bow", "armour", "tool", "axe", "fish", "raw", "food", "tier:iron",
// plus whatever the item's own "tags" add ("wood", "leather", "gem", "dream").
vector<string> Tags(const ItemDatabase& db, const ItemDef& d);

// Coins and quest items (value 0) never change hands.
bool Tradeable(const ItemDef& d);

// What the shop charges for one.
int   BuyPrice(const ShopDef& shop, const ItemDef& d);
// The best fraction of value the shop pays for this; 0 if it will not buy it.
float SellRate(const ShopDef& shop, const ItemDatabase& db, const ItemDef& d);
// What the shop pays for one; 0 if it will not buy it.
int   SellPrice(const ShopDef& shop, const ItemDatabase& db, const ItemDef& d);

// Whether a stock line is on the shelf yet. With no quest log, gated lines
// are hidden rather than shown.
bool OnShelf(const ShopStock& line, const QuestLog* quests);
// The stock lines on the shelf right now, in shelf order.
vector<const ShopStock*> Shelf(const ShopDef& shop, const QuestLog* quests);

// Buys up to qty: as many as are in stock, affordable and fit in the bag.
TradeOutcome Buy(const ShopDef& shop, ShopLedger& ledger, Inventory& bag,
                 const ItemDatabase& db, const QuestLog* quests,
                 const string& item, int qty);
// Sells up to qty of what the bag holds.
TradeOutcome Sell(const ShopDef& shop, Inventory& bag, const ItemDatabase& db,
                  const string& item, int qty);

const char* Message(TradeResult r);

} // namespace Trade
