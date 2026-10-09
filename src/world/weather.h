#pragma once
#include "../headers.h"

// ---------------------------------------------------------------------------
//  The weather
//
//  Showers come over the land and pass: the sky darkens, the rain comes on,
//  rings open on the water and puddles stand in the ruts, and then it eases
//  off and the ground dries. And at dawn a fog lies low over the land and
//  burns off as the morning goes on.
//
//  All of it is worked out from the clock alone (WorldClock::Seconds, the
//  real seconds of play counting each hour at its own length), so two friends
//  in the same world see the same shower at the same moment without a word
//  passing between their machines, and the self-test can set the clock and
//  ask. Nothing here touches the game: a shower soaks nobody and puts out no
//  fire. It is the air's business (Ambience, World::ScreenFrame, Audio).
// ---------------------------------------------------------------------------

namespace Weather {

// How hard it is raining, 0 (dry) to 1 (a downpour), at this moment of play.
// The time is cut into stretches of SLOT seconds; in some of them a shower
// comes over -- its start, its length and how heavy it is all hashed from
// which stretch it is -- coming on over half a minute and easing off over
// most of one.
constexpr double SLOT = 900.0;
float Rain(double seconds);
// How wet the ground still is, 0..1: it gets wet within a minute of the rain
// starting and takes four to dry once it has stopped.
float Wet(double seconds);
// When the shower that is falling (or the next one) starts and ends, in the
// same seconds; for the self-test. False when none falls in this stretch.
bool ShowerIn(long long slot, double& start, double& end, float& heavy);

// The morning's fog, 0..1, by the hour: rising before dawn, thickest from
// half past five to seven, and gone by half past nine.
float MorningFog(float hour);

// Whether showers fall on a map whose air is `ambient`: fields, towns, woods
// and the wetlands -- not the mountain's snow, the burnt land, the dreams,
// under the ground or in the Primordium. Indoors it rains outside: heard,
// and not seen.
bool RainsOn(const string& ambient);

// For looking at it (the --rain flag) and for the self-test: a shower this
// hard everywhere it can fall, whatever the clock says; below zero, the
// clock's own weather again.
void Force(float rain);

}   // namespace Weather
