#include "game.h"

int main(int argc, char* args[]) {
    Game game;

    if (!game.Start(argc, args)) return -1;
    game.Loop();
    return 0;
}
