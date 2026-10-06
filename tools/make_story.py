"""Writes data/story.json: the prologue and Act I from the user's screenplay
(Screenplay.md), scene by scene, as StoryDirector plays it. The lines are the
script's own. This is the source: edit a scene here and run
`python tools/make_story.py`, rather than editing data/story.json."""
import json
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', 'story.json')


def when(flags=(), nots=()):
    w = {}
    if flags:
        w['flags'] = list(flags)
    if nots:
        w['not'] = list(nots)
    return w


def say(who, text, **k):
    return dict({'do': 'say', 'who': who, 'text': text}, **k)


def narrate(text, **k):
    return dict({'do': 'narrate', 'text': text}, **k)


def flag(f):
    return {'do': 'flag', 'set': f}


def bars(on, time=0.6, wait=False):
    return {'do': 'bars', 'on': on, 'time': time, 'wait': wait}


def fade(to, time=1.0, wait=True):
    return {'do': 'fade', 'to': to, 'time': time, 'wait': wait}


def cam(at, time=0.0, wait=True, **k):
    return dict({'do': 'camera', 'at': at, 'time': time, 'wait': wait}, **k)


def wait(t):
    return {'do': 'wait', 'time': t}


def end_control():
    return [bars(False), {'do': 'camera', 'release': True}]


ACTORS = {
    'vexel': {'sprite': 'vexel', 'name': '???',
              'named': [{'when': when(['PRO_13_NAME_REVEAL']), 'name': 'Vexel Von Finch'}]},
    'cart': {'image': 'assets/props/handcart.png', 'name': 'Cart'},
    'villager1': {'sprite': 'citizen2', 'name': 'Villager'},
    'villager2': {'sprite': 'citizen1', 'name': 'Villager'},
    'wife': {'sprite': 'citizen1', 'name': 'Villager'},
    'child': {'sprite': 'citizen2', 'name': 'Child', 'scale': 0.72},
    'elder_bed': {'image': 'assets/props/bed_sleeper_elder.png', 'name': ''},
    'goer1': {'sprite': 'citizen1', 'name': 'Villager'},
    'goer2': {'sprite': 'citizen2', 'name': 'Villager'},
    'goer3': {'sprite': 'fighter2', 'name': 'Villager'},
    'mattress_afloat': {'image': 'assets/props/mattress.png', 'name': ''},
    'chains_afloat': {'image': 'assets/props/wall_chains.png', 'name': ''},
    'silhouette': {'image': 'assets/props/window_silhouette.png', 'name': ''},
    # The inn's bed from its pillow down, laid over whoever is lying in it: from
    # the front, someone on their back in a bed is a head on a pillow.
    'blanket': {'image': 'assets/props/bed_single.png', 'from_row': 33, 'name': ''},
}

TIPS = {
    'move': {'title': 'Getting about',
             'text': 'Walk with {Move}, and hold {Sprint} to run. {Interact} talks, opens and works. Your '
                     'journal, {Journal}, keeps what you have been asked to do, and the arrow at the edge of the '
                     'screen points the way to it.'},
    'hearty_meal': {'title': 'Eating',
                    'text': 'Food heals. The meal Bess gave you is to hand at the bottom left: {Eat} eats it '
                            'without opening your pack, even in a fight. {Next} changes what is to hand.'},
    'reverie': {'title': 'The Reverie',
                'text': 'You are dreaming. Nothing in a dream can kill you, and it ends at dawn -- or sooner, at '
                        'a Waking Stone.'},
    'waking_stone': {'title': 'Waking Stone',
                     'text': 'A stone that pulses with soft light. {Interact} on it and you wake, where you lay '
                             'down. There is one in every dream.'},
    'echo': {'title': 'Echo', 'text': 'What you change in the Reverie carries over into Solace.'},
    'combat': {'title': 'Fighting',
               'text': '{Light} swings and {Heavy} strikes hard. A blow being wound up glows: step out of its way. '
                       '{Guard} guards -- or, with a bow in your hands, rolls. {Eat} eats what is to hand.'},
    'asleep_town': {'title': 'Havenbrook sleeps',
                    'text': 'Its people, its shops and its trades will keep until it wakes. The gates are open: '
                            'the world beyond them is not asleep.'},
}

S = []

# ---------------------------------------------------------------------------------------------
#  Scene 1 -- Found on the Road
# ---------------------------------------------------------------------------------------------
S.append({'id': 'pro_01_road', 'on': 'new_game', 'then': 'pro_02_wake', 'steps': [
    bars(True, 0.0, True), fade('black', 0.0),
    {'do': 'clock', 'hour': 4.1},
    {'do': 'player', 'at': 'pro_road', 'face': 'right', 'pose': 'lie'},
    {'do': 'fx', 'kind': 'veil', 'to': 1.0},
    cam('pro_road_cam', zoom=1.5),
    {'do': 'music', 'cue': 'ominous', 'fade': 3.0},
    fade('clear', 3.0),
    wait(2.5),
    # The sky lightens, and the Reverie dissolves into daylight as dawn breaks.
    {'do': 'timelapse', 'to': 6.4, 'time': 6.5, 'wait': False},
    {'do': 'fx', 'kind': 'veil', 'to': 0.0, 'time': 6.5, 'wait': False},
    {'do': 'music', 'cue': '', 'fade': 4.0},
    wait(3.4),
    # Two villagers with a hand-drawn cart, up the road from the town.
    {'do': 'spawn', 'actor': 'cart', 'at': 'pro_cart_from'},
    {'do': 'spawn', 'actor': 'villager1', 'at': 'pro_cart_from', 'dx': -11, 'dy': 34, 'face': 'up'},
    {'do': 'spawn', 'actor': 'villager2', 'at': 'pro_cart_from', 'dx': 11, 'dy': 34, 'face': 'up'},
    {'do': 'walk', 'who': 'cart', 'at': 'pro_cart_stop', 'speed': 46, 'wait': False},
    {'do': 'walk', 'who': 'villager1', 'at': 'pro_cart_stop', 'dx': -11, 'dy': 34, 'speed': 46, 'wait': False},
    {'do': 'walk', 'who': 'villager2', 'at': 'pro_cart_stop', 'dx': 11, 'dy': 34, 'speed': 46, 'wait': False},
    {'do': 'wait', 'for': 'all'},
    {'do': 'face', 'who': 'villager1', 'toward': 'player'},
    {'do': 'face', 'who': 'villager2', 'toward': 'player'},
    say('villager1', 'Still breathing. Help me get them up.'),
    say('villager2', 'Out here alone all night? Another one, you think?'),
    say('villager1', "Don't say that. Just get them to the tavern."),
    # Lifted onto the cart.
    fade('black', 0.6),
    {'do': 'attach', 'to': 'cart', 'dx': 0, 'dy': -6},
    {'do': 'player', 'lift': 9, 'bias': 8},
    {'do': 'face', 'who': 'villager1', 'dir': 'down'},
    {'do': 'face', 'who': 'villager2', 'dir': 'down'},
    fade('clear', 0.6),
    {'do': 'camera', 'follow': 'cart'},
    {'do': 'walk', 'who': 'cart', 'at': 'pro_cart_mid', 'speed': 40, 'wait': False},
    {'do': 'walk', 'who': 'villager1', 'at': 'pro_cart_mid', 'dx': -11, 'dy': 34, 'speed': 40, 'wait': False},
    {'do': 'walk', 'who': 'villager2', 'at': 'pro_cart_mid', 'dx': 11, 'dy': 34, 'speed': 40, 'wait': False},
    {'do': 'wait', 'for': 'all'},
    {'do': 'walk', 'who': 'cart', 'at': 'pro_cart_away', 'speed': 40, 'wait': False},
    {'do': 'walk', 'who': 'villager1', 'at': 'pro_cart_away', 'dx': -11, 'dy': 34, 'speed': 40, 'wait': False},
    {'do': 'walk', 'who': 'villager2', 'at': 'pro_cart_away', 'dx': 11, 'dy': 34, 'speed': 40, 'wait': False},
    wait(2.2),
    fade('black', 1.8),
    {'do': 'detach'},
    flag('PRO_01_INTRO_DONE'),
]})

# ---------------------------------------------------------------------------------------------
#  Scene 2 -- The Tavern
# ---------------------------------------------------------------------------------------------
S.append({'id': 'pro_02_wake', 'steps': [
    {'do': 'map', 'map': 'house_inn_upper', 'at': 'pro_bed_lie'},
    {'do': 'clock', 'hour': 8.2},
    {'do': 'player', 'at': 'pro_bed_lie', 'face': 'down', 'pose': 'lie', 'lift': 0, 'bias': 26},
    {'do': 'spawn', 'actor': 'blanket', 'at': [150, 250], 'bias': 34},
    cam('pro_upper_cam', zoom=1.5),
    fade('clear', 2.0),
    wait(1.4),
    {'do': 'pose', 'who': 'player', 'clip': 'sit_up', 'wait': True},
    wait(0.6),
    fade('black', 0.3),
    {'do': 'remove', 'actor': 'blanket'},
    {'do': 'player', 'at': 'pro_bed_stand', 'pose': '', 'bias': 0, 'face': 'up'},
    cam('pro_bed_stand', zoom=1.0),
    fade('clear', 0.3),
    {'do': 'note', 'title': 'A folded note',
     'text': 'If you wake before I come to see you, come to the Mayor\'s Hall. We must speak!'},
    {'do': 'quest', 'start': 'q_pro_meet_mayor'},
    flag('PRO_02_NOTE_READ'),
    *end_control(),
    {'do': 'tip', 'id': 'move'},
]})

S.append({'id': 'pro_03_bess', 'on': {'near': 'from_upstairs', 'map': 'house_inn', 'radius': 70},
          'when': when(['PRO_02_NOTE_READ'], ['PRO_03_TAVERN_DONE']), 'steps': [
    bars(True),
    cam('pro_inn_cam', 0.8, False),
    {'do': 'face', 'who': 'npc_cook', 'toward': 'player'},
    wait(0.5),
    {'do': 'walk', 'who': 'npc_cook', 'at': 'pro_bess_meet', 'speed': 82},
    {'do': 'face', 'who': 'npc_cook', 'toward': 'player'},
    {'do': 'face', 'who': 'player', 'toward': 'npc_cook'},
    say('npc_cook', "Oh, thank goodness, you're up! How are you feeling? Any dizziness?"),
    say('npc_cook', 'Two of our folk found you out on the road at first light, flat on your back. We thought the worst.'),
    say('npc_cook', "The Mayor's been waiting on you. He'll want to know you're awake."),
    say('npc_cook', "But you're not leaving on an empty stomach. Here, take these with you. No arguments."),
    {'do': 'give', 'item': 'hearty_meal', 'qty': 2},
    {'do': 'sfx', 'id': 'pickup'},
    say('npc_cook', "The Mayor's Hall is just up the main road. Go on, now."),
    flag('PRO_03_TAVERN_DONE'),
    {'do': 'walk', 'who': 'npc_cook', 'at': [512, 168], 'speed': 90, 'wait': False},
    *end_control(),
    {'do': 'wait', 'for': 'all'},
    {'do': 'tip', 'id': 'hearty_meal'},
]})

# ---------------------------------------------------------------------------------------------
#  Scene 3 -- Havenbrook
# ---------------------------------------------------------------------------------------------
S.append({'id': 'pro_04_pan', 'on': {'enter': 'town_havenbrook', 'spawn': 'from_house_inn'},
          'when': when(['PRO_03_TAVERN_DONE'], ['PRO_04_TOWN_PAN']), 'steps': [
    bars(True),
    {'do': 'music', 'cue': 'town', 'fade': 2.0},
    cam('pan_inn', 1.4, zoom=0.75),
    cam('pan_mayor', 2.6),
    cam('pan_guild', 2.4),
    cam('pan_homes', 3.2),
    cam('pan_well', 2.8),
    cam('player', 2.2, zoom=1.0),
    flag('PRO_04_TOWN_PAN'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scene 4 -- The Mayor's Story
# ---------------------------------------------------------------------------------------------
S.append({'id': 'pro_05_mayor', 'on': {'talk': 'npc_mayor', 'map': 'mayor_hall'},
          'when': when(['PRO_02_NOTE_READ'], ['PRO_05_MAYOR_TALK']), 'steps': [
    bars(True),
    cam('pro_mayor_cam', 0.8, False),
    {'do': 'walk', 'who': 'player', 'at': 'pro_mayor_listen', 'speed': 70},
    {'do': 'face', 'who': 'player', 'dir': 'up'},
    {'do': 'face', 'who': 'npc_mayor', 'dir': 'down'},
    say('npc_mayor', "You're awake. Thank the stars. Please, come in."),
    say('npc_mayor', "My people found you on the north road at dawn, out cold. When they told me, I feared you'd "
                     "caught whatever has taken the others."),
    say('npc_mayor', 'Let me show you what I mean.'),
    # --- the montage: a day and a night between each ---
    fade('black', 1.2),
    {'do': 'clock', 'save': True},
    {'do': 'music', 'cue': 'montage', 'fade': 2.0},
    {'do': 'player', 'hidden': True},
    # 1. A woman shakes her husband in bed. He breathes, but doesn't stir.
    {'do': 'map', 'map': 'house_sleeper', 'spawn': 'entrance'},
    {'do': 'clock', 'hour': 8.0},
    {'do': 'player', 'hidden': True},
    {'do': 'spawn', 'actor': 'wife', 'at': 'sleeper_side', 'face': 'left', 'pose': 'shake'},
    cam('sleeper_cam', zoom=1.6),
    fade('clear', 1.2),
    wait(3.4),
    fade('black', 1.0),
    # Night falls. Morning.
    {'do': 'map', 'map': 'town_havenbrook', 'spawn': 'default'},
    {'do': 'player', 'hidden': True},
    cam('mont_sky', zoom=0.7),
    fade('clear', 0.8),
    {'do': 'timelapse', 'to': 22.0, 'time': 2.2},
    {'do': 'timelapse', 'to': 12.0, 'time': 2.2},
    # 2. Two more homes have shutters still closed at midday.
    cam('mont_houses', 2.0),
    wait(2.6),
    fade('black', 1.0),
    # Night falls. Morning. 3. A child sits beside a sleeping grandparent, waiting.
    cam('mont_sky'),
    fade('clear', 0.6),
    {'do': 'timelapse', 'to': 22.0, 'time': 2.0},
    {'do': 'timelapse', 'to': 9.0, 'time': 2.0},
    fade('black', 0.8),
    {'do': 'map', 'map': 'house_elder', 'spawn': 'default'},
    {'do': 'player', 'hidden': True},
    {'do': 'spawn', 'actor': 'elder_bed', 'at': 'mont_bed', 'bias': 2},
    {'do': 'spawn', 'actor': 'child', 'at': 'mont_child', 'face': 'left'},
    cam('mont_cam', zoom=1.7),
    fade('clear', 1.0),
    wait(3.4),
    fade('black', 1.0),
    # Night falls. Morning. 4. The market square is half empty.
    {'do': 'map', 'map': 'town_havenbrook', 'spawn': 'default'},
    {'do': 'player', 'hidden': True},
    cam('mont_sky', zoom=0.7),
    fade('clear', 0.6),
    {'do': 'timelapse', 'to': 22.0, 'time': 2.0},
    {'do': 'timelapse', 'to': 10.0, 'time': 2.0},
    {'do': 'spawn', 'actor': 'goer1', 'at': 'mont_a', 'face': 'right'},
    {'do': 'spawn', 'actor': 'goer2', 'at': 'mont_b', 'face': 'up'},
    {'do': 'spawn', 'actor': 'goer3', 'at': 'mont_c', 'face': 'left'},
    cam('mont_square', 2.2, zoom=0.85),
    wait(2.8),
    fade('black', 1.2),
    # --- back to the hall ---
    {'do': 'clock', 'restore': True},
    {'do': 'map', 'map': 'mayor_hall', 'at': 'pro_mayor_listen'},
    {'do': 'player', 'hidden': False, 'face': 'up'},
    cam('pro_mayor_cam', zoom=1.0),
    {'do': 'music', 'cue': '', 'fade': 3.0},
    fade('clear', 1.2),
    say('npc_mayor', "It started a few weeks ago. Every morning, fewer doors open. They breathe, they're warm, "
                     "but nothing wakes them."),
    say('npc_mayor', "I don't know what's causing it. Half the town is so afraid of it that they've stopped "
                     "sleeping altogether. Days without rest, and sooner or later their bodies give in. I haven't "
                     "slept properly in days myself. If I close my eyes, I may not open them again."),
    say('npc_mayor', 'But you went down and came back up. Whatever this is, it let you go.'),
    say('npc_mayor', "Go into one of the homes and try to wake someone. If you can reach them, there's hope for "
                     "all of us."),
    {'do': 'quest', 'start': 'q_pro_wake_sleeper'},
    flag('PRO_05_MAYOR_TALK'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scene 5 -- The Stranger at the Bedside
# ---------------------------------------------------------------------------------------------
S.append({'id': 'pro_06_hush', 'on': {'enter': 'house_sleeper'},
          'when': when(['PRO_05_MAYOR_TALK'], ['PRO_06_STRANGER_SEEN']), 'steps': [
    {'do': 'music', 'cue': 'hum', 'fade': 1.2},
]})
S.append({'id': 'pro_06_stranger', 'on': {'near': 'sleeper_near', 'map': 'house_sleeper', 'radius': 92},
          'when': when(['PRO_05_MAYOR_TALK'], ['PRO_06_STRANGER_SEEN']), 'steps': [
    bars(True),
    cam('sleeper_cam', 1.4, False, zoom=1.5),
    wait(1.4),
    # He slowly turns his head toward the player, and says nothing. A long beat.
    {'do': 'face', 'who': 'npc_stranger', 'dir': 'down'},
    wait(2.6),
    {'do': 'fx', 'kind': 'vanish', 'who': 'npc_stranger', 'time': 1.2},
    {'do': 'show', 'who': 'npc_stranger', 'alpha': 0},
    flag('PRO_06_STRANGER_SEEN'),
    wait(0.8),
    cam('player', 0.8, zoom=1.0),
    *end_control(),
]})
for i, line in enumerate(["You shake them gently. Nothing.",
                          "You call out to them. Their breathing doesn't change.",
                          "Their eyes move beneath their lids, as if they're watching something far away."]):
    before = ['PRO_06_STRANGER_SEEN'] + (['PRO_SHAKE_%d' % i] if i else [])
    after = 'PRO_SHAKE_%d' % (i + 1) if i < 2 else 'PRO_07_SLEEPER_FAIL'
    S.append({'id': 'pro_07_shake_%d' % (i + 1), 'on': {'use': 'sleeper_bed', 'map': 'house_sleeper'},
              'when': when(before, [after]), 'steps': [narrate(line), flag(after)]})
S.append({'id': 'pro_07_sleeps_on', 'on': {'use': 'sleeper_bed', 'map': 'house_sleeper'},
          'when': when(['PRO_07_SLEEPER_FAIL']), 'steps': [narrate('He sleeps on, and does not wake.')]})

# ---------------------------------------------------------------------------------------------
#  Scene 6 -- The Silent Town
# ---------------------------------------------------------------------------------------------
S.append({'id': 'pro_08_hall', 'on': {'enter': 'mayor_hall'},
          'when': when(['PRO_07_SLEEPER_FAIL'], ['PRO_08_MAYOR_ASLEEP']), 'steps': [
    {'do': 'music', 'cue': 'hum', 'fade': 1.5},
]})
for i, line in enumerate(["You shake his shoulder. His head lolls to one side.",
                          "You call his name. Nothing.",
                          "He fought it as long as he could."]):
    before = ['PRO_07_SLEEPER_FAIL'] + (['PRO_MAYOR_SHAKE_%d' % i] if i else [])
    after = 'PRO_MAYOR_SHAKE_%d' % (i + 1) if i < 2 else 'PRO_08_MAYOR_ASLEEP'
    steps = [narrate(line), flag(after)]
    if i == 2:
        # Days without sleep have finally overtaken them: the whole town, at once.
        steps += [flag('HAVENBROOK_ASLEEP'), {'do': 'music', 'cue': '', 'fade': 2.5}]
    S.append({'id': 'pro_08_mayor_%d' % (i + 1), 'on': {'talk': 'npc_mayor', 'map': 'mayor_hall'},
              'when': when(before, [after]), 'steps': steps})

S.append({'id': 'pro_09_square', 'on': {'near': 'square_centre', 'map': 'town_havenbrook', 'radius': 270},
          'when': when(['PRO_08_MAYOR_ASLEEP'], ['PRO_09_ABDUCTED']), 'then': 'pro_10_cell', 'steps': [
    bars(True),
    {'do': 'spawn', 'actor': 'vexel', 'at': 'square_stranger', 'face': 'left', 'alpha': 0},
    # Between the two of them, wherever the player came into the square from --
    # pulled back, and a little low, so the stranger stands clear of the lines.
    cam(['player', 'vexel'], 1.2, False, zoom=0.75, dy=50),
    {'do': 'fx', 'kind': 'appear', 'who': 'vexel', 'time': 1.1},
    # He turns slowly, taking in the scene, and stops when he faces the player.
    wait(0.9), {'do': 'face', 'who': 'vexel', 'dir': 'up'},
    wait(0.9), {'do': 'face', 'who': 'vexel', 'dir': 'right'},
    wait(0.9), {'do': 'face', 'who': 'vexel', 'toward': 'player'},
    {'do': 'face', 'who': 'player', 'toward': 'vexel'},
    wait(0.8),
    say('vexel', 'You...'),
    say('vexel', '...are still awake.'),
    say('vexel', "No. You've woken, haven't you?"),
    say('vexel', "You're the one they carted in this morning. Out cold on the road."),
    say('vexel', 'Then you must be one of... them.'),
    {'do': 'walk', 'who': 'vexel', 'toward': 'player', 'dist': 26, 'speed': 30},
    say('vexel', 'If you want these people to wake again, you will help me.'),
    say('vexel', 'Come with me.'),
    # The player turns and runs. He raises one hand.
    {'do': 'walk', 'who': 'player', 'toward': 'vexel', 'dist': -150, 'speed': 150, 'clip': 'run', 'wait': False},
    wait(0.35),
    {'do': 'pose', 'who': 'vexel', 'clip': 'attack', 'wait': False},
    wait(0.75),
    {'do': 'fx', 'kind': 'take'},
    fade('white', 0.45),
    flag('PRO_09_ABDUCTED'),
    {'do': 'music', 'cue': '', 'fade': 0.5},
    wait(0.5),
    fade('black', 0.0),
    wait(0.8),
]})

# ---------------------------------------------------------------------------------------------
#  Scene 7 -- The Cell
# ---------------------------------------------------------------------------------------------
S.append({'id': 'pro_10_cell', 'steps': [
    {'do': 'map', 'map': 'mansion_cells', 'spawn': 'pro_cell_mid'},
    {'do': 'clock', 'hour': 19.6},
    {'do': 'player', 'at': 'pro_cell_mid', 'face': 'down', 'pose': '', 'hidden': False, 'lift': 0, 'bias': 0},
    {'do': 'spawn', 'actor': 'vexel', 'at': 'pro_bars_out', 'face': 'up'},
    cam('pro_cell_cam', zoom=1.0),
    {'do': 'music', 'cue': 'ominous', 'fade': 2.5},
    fade('clear', 1.6),
    wait(0.6),
    say('vexel', 'Forgive my lack of hospitality. Good furniture is hard to come by these days.'),
    say('vexel', "I'd like to try an experiment. Sleep here tonight, and let us see what happens to you."),
    say('vexel', "I'm very curious what you'll find."),
    # He walks away down the corridor; his footsteps fade.
    {'do': 'face', 'who': 'vexel', 'dir': 'right'},
    {'do': 'walk', 'who': 'vexel', 'at': 'pro_corridor_east', 'speed': 40},
    {'do': 'remove', 'actor': 'vexel'},
    {'do': 'quest', 'start': 'q_pro_experiment'},
    flag('PRO_10_IN_CELL'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scene 8 -- The First Echo
# ---------------------------------------------------------------------------------------------
# Sleeps -- and sleeps again, for as long as the door has not been opened in the dream.
S.append({'id': 'pro_11_sleep', 'on': {'use': 'pro_mattress', 'map': 'mansion_cells'},
          'when': when(['PRO_10_IN_CELL'], ['ECHO_CELL_DOOR_OPEN', 'PRO_12_WAKING_STONE']), 'steps': [
    bars(True),
    {'do': 'walk', 'who': 'player', 'at': 'pro_mattress', 'speed': 60},
    {'do': 'player', 'pose': 'lie', 'face': 'right', 'bias': 6},
    {'do': 'music', 'cue': '', 'fade': 1.5},
    wait(1.4),
    flag('PRO_11_FIRST_REVERIE'),
    {'do': 'fx', 'kind': 'dream', 'map': 'mansion_cells_dream', 'spawn': 'arrival'},
    wait(1.4),
    bars(False),
]})
S.append({'id': 'pro_11_dream', 'on': {'enter': 'mansion_cells_dream'},
          'when': when(['PRO_11_FIRST_REVERIE'], ['PRO_12_WAKING_STONE']), 'steps': [
    # The same room, subtly wrong: the mattress afloat, the chains drifting up.
    {'do': 'spawn', 'actor': 'mattress_afloat', 'at': 'pro_mattress', 'lift': 9, 'bob': 3, 'keep': True},
    {'do': 'spawn', 'actor': 'chains_afloat', 'at': [150, 70], 'bob': 5, 'keep': True},
    {'do': 'music', 'cue': 'dream', 'fade': 2.5},
    {'do': 'tip', 'id': 'reverie'},
]})
S.append({'id': 'pro_11_stone', 'on': {'near': 'waking_stone_near', 'map': 'mansion_cells_dream', 'radius': 110},
          'when': when(['PRO_11_FIRST_REVERIE'], ['PRO_TIP_STONE']), 'steps': [
    {'do': 'tip', 'id': 'waking_stone'}, flag('PRO_TIP_STONE'),
]})
S.append({'id': 'pro_12_wake', 'on': {'enter': 'mansion_cells'},
          'when': when(['PRO_11_FIRST_REVERIE', 'ECHO_CELL_DOOR_OPEN'], ['PRO_12_WAKING_STONE']), 'steps': [
    bars(True, 0.0),
    {'do': 'player', 'at': 'pro_mattress', 'pose': 'lie', 'face': 'right', 'bias': 6},
    cam('pro_cell_cam'),
    {'do': 'music', 'cue': '', 'fade': 1.0},
    wait(1.4),
    {'do': 'pose', 'who': 'player', 'clip': 'sit_up', 'wait': True},
    wait(0.4),
    # They look up: the cell door stands wide open, just as they left it in the dream.
    cam('pro_cell_door_in', 1.3),
    wait(1.4),
    fade('black', 0.25),
    {'do': 'player', 'pose': '', 'at': 'pro_cell_mid', 'bias': 0, 'face': 'down'},
    fade('clear', 0.25),
    flag('PRO_12_WAKING_STONE'),
    *end_control(),
]})
# Woken -- at the stone, or by the dawn -- with the door still shut in the dream: still shut here.
S.append({'id': 'pro_12_wake_shut', 'on': {'enter': 'mansion_cells'},
          'when': when(['PRO_11_FIRST_REVERIE'], ['ECHO_CELL_DOOR_OPEN', 'PRO_12_WAKING_STONE']), 'steps': [
    bars(True, 0.0),
    {'do': 'player', 'at': 'pro_mattress', 'pose': 'lie', 'face': 'right', 'bias': 6},
    cam('pro_cell_cam'),
    wait(1.2),
    {'do': 'pose', 'who': 'player', 'clip': 'sit_up', 'wait': True},
    cam('pro_cell_door_in', 1.0),
    narrate('The door is still bolted. Whatever has to be done, it has to be done in there.'),
    fade('black', 0.25),
    {'do': 'player', 'pose': '', 'at': 'pro_cell_mid', 'bias': 0, 'face': 'down'},
    fade('clear', 0.25),
    *end_control(),
]})
S.append({'id': 'pro_12_not_again', 'on': {'use': 'pro_mattress', 'map': 'mansion_cells'},
          'when': when(['PRO_12_WAKING_STONE']), 'steps': [narrate('Not here. Not again.')]})

# ---------------------------------------------------------------------------------------------
#  Scene 9 -- Vigil
# ---------------------------------------------------------------------------------------------
S.append({'id': 'pro_13_voice', 'on': {'near': 'out_of_cell', 'map': 'mansion_cells', 'radius': 40},
          'when': when(['PRO_12_WAKING_STONE'], ['PRO_VIGIL_MET']), 'steps': [
    bars(True),
    say('npc_vigil', 'You... you opened that door, didn\'t you?', name='Old Man'),
    {'do': 'camera', 'follow': 'player'},
    {'do': 'walk', 'who': 'player', 'at': 'vigil_front', 'speed': 62},
    {'do': 'face', 'who': 'player', 'dir': 'up'},
    {'do': 'pose', 'who': 'npc_vigil', 'clip': 'attack', 'wait': False},
    say('npc_vigil', "(cough, cough) You're a Dreamwalker."),
    say('npc_vigil', "What you did just now is called an Echo. What you change in your dreams carries over into the "
                     "waking world. You're a rare one."),
    say('npc_vigil', "Quick, the cell across from mine. There's a chest. Take a weapon. Your dreams can turn "
                     "dangerous, and you don't want to be caught without protection."),
    {'do': 'quest', 'start': 'q_pro_arm'},
    flag('PRO_VIGIL_MET'),
    *end_control(),
    {'do': 'tip', 'id': 'echo'},
]})
S.append({'id': 'pro_13_name', 'on': {'near': 'vigil_front', 'map': 'mansion_cells', 'radius': 60},
          'when': when(['WEAPON_CHOSEN', 'PRO_VIGIL_MET'], ['PRO_13_NAME_REVEAL']), 'steps': [
    bars(True),
    cam('vigil_front', 0.6, False),
    {'do': 'walk', 'who': 'player', 'at': 'pro_reach_door', 'speed': 50},
    {'do': 'face', 'who': 'player', 'dir': 'up'},
    {'do': 'pose', 'who': 'npc_vigil', 'clip': 'attack', 'wait': False},
    say('npc_vigil', "No. Don't waste your strength on me, please. (cough)"),
    say('npc_vigil', 'Save Havenbrook from whatever has its hold on them.'),
    say('npc_vigil', 'My name is Vigil.'),
    {'do': 'name', 'who': 'npc_vigil', 'text': 'Vigil'},
    say('npc_vigil', "And the one who brought us here, Vexel Von Finch... I don't know that you should trust him."),
    flag('PRO_13_NAME_REVEAL'),
    say('npc_vigil', 'One more thing. Take this.'),
    narrate('Vigil reaches inside his tattered coat and draws out a woven hoop strung with pale thread and a few '
            'worn feathers. It hums faintly. He passes it through the bars into your hands.'),
    {'do': 'sfx', 'id': 'chime', 'volume': 0.35, 'pitch': 0.8},
    {'do': 'give', 'item': 'vigil_dreamcatcher'},
    flag('PRO_14_DREAMCATCHER'),
    say('npc_vigil', "It's the last thing I have left from the old days. Keep it close. Don't ask me what it does. "
                     "When the time comes, you'll know what to do with it."),
    narrate('He lets go of the bars and sags back into the shadows of his cell.'),
    say('npc_vigil', 'Take the stairs up. Get out of this house.'),
    {'do': 'quest', 'start': 'q_pro_escape'},
    *end_control(),
    {'do': 'tip', 'id': 'dreamcatcher'},
]})
# A game saved past the name before the Dreamcatcher was in the scene: it is
# handed over all the same, the next time the story looks.
S.append({'id': 'pro_14_dreamcatcher_owed', 'on': {'flag': 'PRO_13_NAME_REVEAL'},
          'when': when([], ['PRO_14_DREAMCATCHER']), 'steps': [
    {'do': 'give', 'item': 'vigil_dreamcatcher'},
    flag('PRO_14_DREAMCATCHER'),
    {'do': 'tip', 'id': 'dreamcatcher'},
]})

# ---------------------------------------------------------------------------------------------
#  Scene 10 -- Escape
# ---------------------------------------------------------------------------------------------
S.append({'id': 'pro_14_foyer', 'on': {'near': 'foyer_middle', 'map': 'mansion_foyer', 'radius': 96},
          'when': when([], ['PRO_FOYER_WAKE']), 'steps': [
    bars(True),
    cam('foyer_cam', 1.0),
    {'do': 'sfx', 'id': 'grind'},
    wait(0.7),
    # With a grinding of metal their visors light; they step down and turn.
    flag('PRO_FOYER_WAKE'),
    {'do': 'music', 'cue': 'ominous', 'fade': 1.0},
    wait(1.3),
    {'do': 'quest', 'start': 'q_pro_first_blood'},
    *end_control(),
    {'do': 'tip', 'id': 'combat'},
]})
S.append({'id': 'pro_15_outside', 'on': {'enter': 'mansion_grounds'},
          'when': when(['PRO_15_FOYER_CLEARED'], ['PRO_OUTSIDE']), 'steps': [
    {'do': 'music', 'cue': 'escape', 'fade': 0.4},
    {'do': 'fx', 'kind': 'lightning'},
    flag('PRO_OUTSIDE'),
]})
S.append({'id': 'pro_16_end', 'on': {'near': 'gate_end', 'map': 'mansion_grounds', 'radius': 80},
          'when': when(['PRO_OUTSIDE'], ['PRO_COMPLETE']), 'steps': [
    bars(True),
    # Back up toward the house; a lit window on the upper floor.
    cam('grounds_window', 3.6),
    wait(0.8),
    {'do': 'fx', 'kind': 'lightning'},
    {'do': 'spawn', 'actor': 'silhouette', 'at': 'grounds_window', 'bias': 130},
    wait(1.6),
    {'do': 'fx', 'kind': 'lightning', 'volume': 0.7},
    {'do': 'remove', 'actor': 'silhouette'},
    wait(1.2),
    fade('black', 1.6),
    {'do': 'music', 'cue': '', 'fade': 2.0},
    {'do': 'map', 'map': 'town_havenbrook', 'spawn': 'act1_path'},
    {'do': 'clock', 'hour': 7.5, 'next_morning': True},
    {'do': 'player', 'at': 'act1_path', 'face': 'down', 'pose': ''},
    cam('act1_path'),
    # FADE IN: the path in the morning light, the town quiet round it -- and
    # the card held over it.
    wait(0.8),
    fade('clear', 2.2),
    {'do': 'title', 'text': 'PROLOGUE', 'sub': "The Town That Wouldn't Wake", 'time': 4.5},
    flag('PRO_COMPLETE'),
    'act1_card',
]})

# ---------------------------------------------------------------------------------------------
#  Scene 11 -- Elder Vask
# ---------------------------------------------------------------------------------------------
S.append({'id': 'act1_vask', 'on': {'talk': 'npc_vask_porch', 'map': 'town_havenbrook'},
          'when': when(['PRO_COMPLETE'], ['ACT1_01_VASK_FOUND']), 'steps': [
    bars(True),
    cam('npc_vask_porch', 0.8, False),
    narrate("Vask's eyes are shut. He groans, twists in his chair, and mutters as if arguing with someone only "
            "he can see."),
    say('npc_vask_porch', "No... you can't keep me here..."),
    say('npc_vask_porch', 'Get back... get away from them...'),
    say('npc_vask_porch', "(groaning) ...I'm not finished yet..."),
    narrate('He keeps rocking, and never wakes. Wherever he is, he is fighting something there.'),
    flag('ACT1_01_VASK_FOUND'),
    {'do': 'quest', 'start': 'q_act1_vask'},
    *end_control(),
]})

# =============================================================================================
#  ACT I -- Learning the Rules (Screenplay.md, scenes 17-50)
#
#  Every scene here is the host's: in company, friends play on in Solace while
#  the story runs (StoryDirector is not run for a guest, and a story object
#  tells a friend it is the host's to use). Flags are only ever set: a friend's
#  machine hears a flag set and never one cleared, so whatever changes, a
#  newer flag says so.
# =============================================================================================

S = [x for x in S if x['id'] != 'act1_vask']

ACTORS.update({
    'ring1': {'sprite': 'hushed', 'name': 'Hushed'},
    'ring2': {'sprite': 'hushed', 'name': 'Hushed'},
    'ring3': {'sprite': 'hushed', 'name': 'Hushed'},
    'ring4': {'sprite': 'hushed', 'name': 'Hushed'},
    'captive_wood': {'sprite': 'citizen1', 'name': 'Sawyer Jessa'},
    'captive_water': {'sprite': 'citizen1', 'name': 'Angler Sula'},
    'captive_mine': {'sprite': 'citizen2', 'name': 'Pitmaster Dorn'},
    'log': {'image': 'assets/icons/logs.png', 'name': ''},
    'fish': {'image': 'assets/icons/tiers/raw_minnow.png', 'name': ''},
    'ore': {'image': 'assets/icons/copper_ore.png', 'name': ''},
    'bobber': {'image': 'assets/props/bobber.png', 'name': ''},
    'dreamcatcher_held': {'image': 'assets/icons/vigil_dreamcatcher.png', 'name': ''},
    # Scene 59, the echo of the struggle in Mara's house -- and the one figure
    # in it that is not an echo (the rival, nameless and hooded until Act II).
    'mara_echo': {'sprite': 'citizen1', 'name': 'Mara'},
    'echo_hushed1': {'sprite': 'hushed', 'name': 'Hushed'},
    'echo_hushed2': {'sprite': 'hushed', 'name': 'Hushed'},
    'echo_hushed3': {'sprite': 'hushed', 'name': 'Hushed'},
    'echo_hushed4': {'sprite': 'hushed', 'name': 'Hushed'},
    'echo_knight1': {'sprite': 'animated_armor', 'name': 'Black Knight'},
    'echo_knight2': {'sprite': 'animated_armor', 'name': 'Black Knight'},
    'rival': {'sprite': 'player_warden', 'name': '???'},
    # Scene 62: Wynn in her doorway as the shadow passes.
    'wynn_door': {'sprite': 'citizen1', 'name': 'Wynn'},
})

TIPS.update({
    'dreamcatcher': {'title': 'The Dreamcatcher',
                     'text': "Vigil's Dreamcatcher is in your pack. When it is wanted, the button that would talk "
                             "to someone says Use Dreamcatcher -- and a little bell hangs over them."},
    'story_dream': {'title': "A sleeper's dream",
                    'text': "You are inside somebody else's dream. If it goes badly, you wake beside them, and "
                            "whatever you changed in it stays changed. The Waking Stone lets you out sooner."},
    'gathering': {'title': 'Gathering',
                  'text': 'The woodcutter, the angler and the miner are awake, and each has a bell over them: '
                          'they will teach you their trade. Woodcutting, Fishing and Mining each need their tool.'},
    'dawn_chimes': {'title': 'Dawn Chimes',
                    'text': "Some of Havenbrook's sleepers have a bell over them: the Dreamcatcher can take you "
                            "into their dream, where a small Anchor needs breaking. The Hushed in those dreams are "
                            "always about as strong as you are."},
    'fishing': {'title': 'Fishing',
                'text': 'Cast at the water with {Interact}. The bobber dips once -- not yet -- and then goes under: '
                        'press {Interact} then. With the fish on, hold {Interact} to reel and keep the line in the '
                        'green until it is in. Out of the green too long, the line snaps. The better the fish, the '
                        'harder it fights.'},
    'weak_seams': {'title': 'Soft seams',
                   'text': 'After its flame and its spin the Forge Demon glows gold for a moment. While it glows, '
                           'every blow lands twice as hard.'},
})


def counted(f, n=3):
    return [f + '_' + str(k) for k in range(1, n + 1)]


def white_out(t=0.6):
    return fade('white', t)


def wake(map_id, at=None, **k):
    st = {'do': 'wake', 'map': map_id}
    if at is not None:
        st['at'] = at
    st.update(k)
    return st


def fx(kind, **k):
    return dict({'do': 'fx', 'kind': kind}, **k)


def sfx(i, volume=1.0, pitch=1.0):
    return {'do': 'sfx', 'id': i, 'volume': volume, 'pitch': pitch}


def music(cue, t=1.5):
    return {'do': 'music', 'cue': cue, 'fade': t}


def quest(q):
    return {'do': 'quest', 'start': q}


def count(f, n=3):
    return {'do': 'count', 'flag': f, 'of': n}


def pose(who, clip, wait=False):
    return {'do': 'pose', 'who': who, 'clip': clip, 'wait': wait}


def face(who, toward=None, d=None):
    st = {'do': 'face', 'who': who}
    if toward:
        st['toward'] = toward
    if d:
        st['dir'] = d
    return st


def walk(who, at, speed=60, wait=True, **k):
    return dict({'do': 'walk', 'who': who, 'at': at, 'speed': speed, 'wait': wait}, **k)


def threads(who_text):
    # The Dreamcatcher, catching a dream: the same few beats every time.
    return [
        bars(True),
        {'do': 'spawn', 'actor': 'dreamcatcher_held', 'at': 'player', 'dy': -30, 'lift': 6},
        sfx('chime', 0.4, 0.75),
        narrate(who_text),
        fx('flash', colour=[226, 214, 255], amount=0.5),
        {'do': 'remove', 'actor': 'dreamcatcher_held'},
    ]


def wake_tries(prefix, npc, room, lines, done_flag, first_when, extra=None):
    # "Each press of the interact key shows the next line": three tries, and
    # the third says the Dreamcatcher stirs. A friend is never shown them.
    tries = [prefix + '_TRY1', prefix + '_TRY2']
    S.append({'id': prefix.lower() + '_try1', 'on': {'talk': npc, 'map': room}, 'when': when(first_when, [tries[0]]),
              'steps': [narrate(lines[0]), flag(tries[0])]})
    S.append({'id': prefix.lower() + '_try2', 'on': {'talk': npc, 'map': room}, 'when': when([tries[0]], [tries[1]]),
              'steps': [narrate(lines[1]), flag(tries[1])]})
    S.append({'id': prefix.lower() + '_try3', 'on': {'talk': npc, 'map': room}, 'when': when([tries[1]], [done_flag]),
              'steps': [narrate(lines[2]), *(extra or []), flag(done_flag)]})


# ---------------------------------------------------------------------------------------------
#  Scene 17 -- the ACT I card, Anyone Awake?, and Elder Vask
# ---------------------------------------------------------------------------------------------
ACT1_CARD = [
    # The prologue card fades to black and holds for a beat.
    fade('black', 1.2),
    wait(1.0),
    {'do': 'title', 'text': 'ACT I', 'sub': 'Learning the Rules', 'time': 4.5},
    fade('clear', 1.6),
    flag('ACT1_00_STARTED'),
    quest('q_pro_anyone_awake'),
    *end_control(),
    {'do': 'tip', 'id': 'asleep_town'},
]
for sc in S:
    if sc['id'] == 'pro_16_end':
        i = sc['steps'].index('act1_card')
        sc['steps'][i:i + 1] = ACT1_CARD
# A game saved after the prologue's last card but before Act I's.
S.append({'id': 'act1_card_owed', 'on': {'enter': 'town_havenbrook'},
          'when': when(['PRO_COMPLETE'], ['ACT1_00_STARTED']), 'steps': [
    bars(True, 0.0), fade('black', 0.0), *ACT1_CARD,
]})

S.append({'id': 'act1_vask', 'on': {'talk': 'npc_vask_porch', 'map': 'town_havenbrook'},
          'when': when(['ACT1_00_STARTED'], ['ACT1_01_VASK_FOUND']), 'steps': [
    bars(True),
    cam('npc_vask_porch', 0.8, False),
    narrate("Elder Vask's eyes are shut. He groans, twists in his chair, and mutters as if arguing with someone "
            "only he can see."),
    say('npc_vask_porch', "(groaning) No... you can't keep me here... Get back... get away from them... "
                          "I'm not finished yet..."),
    narrate('He keeps rocking and never wakes. He is half in the Reverie, fighting something there.'),
    flag('ACT1_01_VASK_FOUND'),
    quest('q_act1_vask'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scene 18 -- into Vask's dream
# ---------------------------------------------------------------------------------------------
S.append({'id': 'act1_vask_catch', 'on': {'talk': 'npc_vask_porch', 'map': 'town_havenbrook'},
          'needs': 'vigil_dreamcatcher', 'when': when(['ACT1_01_VASK_FOUND'], ['ACT1_02_DREAM_ENTERED']), 'steps': [
    *threads('The Dreamcatcher warms and glows in your hands. Fine threads of pale light drift out of '
             "Vask's chest and tangle in the hoop. It spins and tightens into a swirling web, and the web pulls "
             'you in.'),
    flag('ACT1_02_DREAM_ENTERED'),
    fx('dream', map='prologue_dream_havenbrook', spawn='arrival', story=True),
    wait(1.4),
    bars(False),
]})
# Back in, after a fall or the stone: the town as it was left.
S.append({'id': 'act1_vask_again', 'on': {'talk': 'npc_vask_porch', 'map': 'town_havenbrook'},
          'needs': 'vigil_dreamcatcher', 'when': when(['ACT1_02_DREAM_ENTERED'], ['ECHO_HAVENBROOK_BELLS']),
          'steps': [
    *threads('The Dreamcatcher warms again, and the web pulls you back in.'),
    fx('dream', map='prologue_dream_havenbrook', spawn='arrival', story=True),
    wait(1.4),
    bars(False),
]})


# ---------------------------------------------------------------------------------------------
#  Scene 19 -- the Guild Hall porch, dreaming
# ---------------------------------------------------------------------------------------------
def ring_round_vask():
    # A ring of Hushed closing on him; he holds them off with his cane.
    out = []
    for k, (dx, dy, f) in enumerate([(-44, 18, 'right'), (44, 18, 'left'), (-30, 46, 'up'), (30, 46, 'up')]):
        out.append({'do': 'spawn', 'actor': 'ring%d' % (k + 1), 'at': 'dream_vask', 'dx': dx, 'dy': dy, 'face': f,
                    'keep': True})
    return out


S.append({'id': 'act1_dream_intro', 'on': {'enter': 'prologue_dream_havenbrook'},
          'when': when(['ACT1_02_DREAM_ENTERED'], ['ACT1_DREAM_INTRO']), 'steps': [
    bars(True, 0.0),
    music('dream_town', 2.5),
    *ring_round_vask(),
    cam('dream_vask_cam', zoom=1.2),
    wait(1.2),
    narrate('The same town, warped by dream logic: houses lean at wrong angles, the sky is a bruised violet, and the '
            'well in the square glows from the inside. Pale, faceless figures drift through the streets, humming a '
            'slow lullaby.'),
    # On his feet in front of his chair, chopping at them with his cane
    # (genmaps: his dream state is the standing sheet, "fend" looping); he
    # holds his guard while he turns to speak, and is at it again after.
    pose('npc_vask_dream', 'fend'),
    say('npc_vask_dream', 'Back, you hollow things! Back!'),
    face('npc_vask_dream', 'player'),
    pose('npc_vask_dream', 'idle'),
    say('npc_vask_dream', "You've got a face. The rest of them don't. Not anymore. They've been pulling at this town "
                          "for days. I can feel the roots: three out along the edges and one thick one in the square. "
                          "Cut them loose, whoever you are. I'll keep this lot busy."),
    face('npc_vask_dream', d='down'),
    pose('npc_vask_dream', 'fend'),
    narrate('The nearest Hushed turn to face you.'),
    flag('ACT1_DREAM_INTRO'),
    quest('q_act1_break_hold'),
    *end_control(),
    {'do': 'tip', 'id': 'story_dream'},
]})
# Back in later: the boss's music if it is up, the dream's otherwise.
S.append({'id': 'act1_dream_boss_again', 'on': {'enter': 'prologue_dream_havenbrook'},
          'when': when(['ACT1_DREAM_INTRO', 'ACT1_VANGUARD_RISE'], ['ACT1_VANGUARD_DOWN', 'ECHO_HAVENBROOK_BELLS']),
          'steps': [*ring_round_vask(), music('boss', 1.0)]})
S.append({'id': 'act1_dream_again', 'on': {'enter': 'prologue_dream_havenbrook'},
          'when': when(['ACT1_DREAM_INTRO'], ['ECHO_HAVENBROOK_BELLS']),
          'steps': [*ring_round_vask(), music('dream_town', 2.0)]})

# ---------------------------------------------------------------------------------------------
#  Scene 20 -- the Nightmare Holds
# ---------------------------------------------------------------------------------------------
HOLDS = [
    ('wood', 'captive_wood', "(faint) I felt the trees falling all night... thank you."),
    ('water', 'captive_water', "(faint) The water kept rising and I couldn't get my line out. Thank you."),
    ('mine', 'captive_mine', "(faint) So dark down there... you let some air in."),
]
for key, captive, line in HOLDS:
    up = key.upper()
    S.append({'id': 'act1_hold_%s_taut' % key, 'on': {'use': 'hold_' + key, 'map': 'prologue_dream_havenbrook'},
              'when': when(['ACT1_DREAM_INTRO'], ['ACT1_HOLD_%s_GUARDS' % up, 'ACT1_HOLD_%s_FREED' % up]),
              'steps': [narrate("The knot's threads are taut and humming, and will not give while its guards "
                                "still stand.")]})
    S.append({'id': 'act1_hold_%s_release' % key, 'on': {'use': 'hold_' + key, 'map': 'prologue_dream_havenbrook'},
              'when': when(['ACT1_HOLD_%s_GUARDS' % up], ['ACT1_HOLD_%s_FREED' % up]), 'steps': [
        bars(True),
        cam('hold_' + key + '_at', 0.5, False),
        narrate('The knot slackens. You pull the threads apart, and they unravel.'),
        sfx('shatter', 0.5, 1.2),
        flag('ACT1_HOLD_%s_FREED' % up),
        {'do': 'spawn', 'actor': captive, 'at': 'hold_' + key + '_at', 'dy': -4, 'alpha': 0.75, 'face': 'down'},
        fx('flash', colour=[255, 236, 200], amount=0.4),
        say(captive, line),
        fx('golden', at='hold_' + key + '_at', amount=0.5),
        fx('vanish', who=captive, sink=False),
        wait(1.1),
        {'do': 'remove', 'actor': captive},
        count('ACT1_03_HOLDS_CLEARED'),
        *end_control(),
    ]})

S.append({'id': 'act1_barrier', 'on': {'flag': 'ACT1_03_HOLDS_CLEARED_3'},
          'when': when([], ['ACT1_04_ANCHOR_UNSEALED']), 'steps': [
    bars(True),
    cam('square_centre', 1.4),
    wait(0.4),
    # The dark barrier round the square cracks and falls away like shattered glass.
    fx('shatter', at='square_centre', radius=150),
    flag('ACT1_04_ANCHOR_UNSEALED'),
    wait(0.8),
    sfx('bell', 0.5, 0.6),
    narrate('A low, bell-like tone hums from the centre of the square. Every Hushed in town goes silent for a '
            'heartbeat. Then the humming returns, louder.'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scene 21 -- the Anchor, and its guardian
# ---------------------------------------------------------------------------------------------
S.append({'id': 'act1_square', 'on': {'near': 'square_centre', 'map': 'prologue_dream_havenbrook', 'radius': 210},
          'when': when(['ACT1_04_ANCHOR_UNSEALED'], ['ACT1_VANGUARD_RISE']), 'steps': [
    bars(True),
    cam('square_anchor_at', 1.0),
    narrate('The square is dominated by the Nightmare Anchor, a colossal knot of black thread and dream smoke rooted '
            'in the ground around the well. Thin strands run from it across every rooftop and into every house, one '
            'for each sleeper. All of Havenbrook is tied to this one thing.'),
    sfx('grind', 0.8, 0.7),
    narrate('The knot shudders. A figure drags itself out of it: a hollow suit of scorched armour, plated in dark, '
            'charred scales, with a cracked visor and a tattered cloak singed black.'),
    flag('ACT1_VANGUARD_RISE'),
    wait(1.4),
    music('boss', 0.6),
    say('npc_vask_dream', '(distant) Steady... steady now...', name='Elder Vask'),
    *end_control(),
]})
S.append({'id': 'act1_vanguard_down', 'on': {'flag': 'ACT1_VANGUARD_DOWN'},
          'when': when([], ['ACT1_SCALE_TAKEN']), 'steps': [
    bars(True),
    wait(1.2),
    narrate('The Ashen Vanguard drops to one knee, its greatsword slipping from its grip. It crumbles into drifting '
            "ash, and leaves a single charred scale on the stones. The Anchor's threads go slack."),
    {'do': 'give', 'item': 'scorched_scale'},
    flag('ACT1_SCALE_TAKEN'),
    music('dream_town', 2.0),
    *end_control(),
]})
S.append({'id': 'act1_anchor_held', 'on': {'use': 'square_anchor', 'map': 'prologue_dream_havenbrook'},
          'when': when(['ACT1_04_ANCHOR_UNSEALED'], ['ACT1_VANGUARD_DOWN']),
          'steps': [narrate('The Anchor cannot be touched while its guardian stands.')]})
S.append({'id': 'act1_anchor_break', 'on': {'use': 'square_anchor', 'map': 'prologue_dream_havenbrook'},
          'when': when(['ACT1_VANGUARD_DOWN'], ['ACT1_05_ANCHOR_DOWN']), 'steps': [
    bars(True),
    cam('square_anchor_at', 0.6),
    narrate('You break the Anchor. The strands across the rooftops snap and fall away one by one, and the knot '
            'unravels into nothing.'),
    fx('shatter', at='square_anchor_at', radius=110),
    flag('ACT1_05_ANCHOR_DOWN'),
    wait(1.0),
    narrate('Where it stood, the dark peels off an old bell frame hidden at the centre of the square: the Dawn Bells, '
            'a cluster of pale bells that glow faintly.'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scenes 21-23 -- the Dawn Bells, the morning, and a fist bumped
# ---------------------------------------------------------------------------------------------
S.append({'id': 'act1_bells', 'on': {'use': 'dawn_bells', 'map': 'prologue_dream_havenbrook'},
          'when': when(['ACT1_05_ANCHOR_DOWN'], ['ECHO_HAVENBROOK_BELLS']), 'steps': [
    bars(True),
    cam('dawn_bells_at', 0.6),
    sfx('bell', 1.0),
    narrate('A deep, clear tone rolls out across the town.'),
    fx('golden', at='dawn_bells_at'),
    narrate('Warm golden light washes outward from the bells across the Reverie. The Hushed in the streets dissolve '
            'into drifting dust.'),
    {'do': 'banish', 'type': 'hushed'},
    wait(1.0),
    flag('ECHO_HAVENBROOK_BELLS'),
    white_out(0.5),
    music('', 0.8),
    wake('town_havenbrook', 'act1_square'),
    {'do': 'player', 'face': 'up'},
    cam('act1_bells_solace'),
    wait(0.6),
    fade('clear', 1.6),
    sfx('bell', 0.35),
    narrate('Morning, in Solace. The bells hang where the old frame stood, still ringing faintly.'),
    cam('npc_sawyer', 0.0),
    narrate('The woodcutter stirs, stretches, and blinks awake...'),
    cam('npc_angler', 0.0),
    narrate('...the angler, at the water...'),
    cam('npc_pitmaster', 0.0),
    narrate('...and the miner, at the pit. The rest of Havenbrook sleeps on.'),
    cam('act1_square', 0.0),
    {'do': 'camera', 'follow': 'player'},
    walk('player', 'vask_front', 64),
    face('player', d='up'),
    cam('vask_porch_cam', 0.6),
    # Awake in his chair, rocking easily, eyes open and calm.
    wait(0.8),
    pose('npc_vask_porch', 'fist'),
    wait(1.0),
    sfx('bump', 0.8),
    fx('flash', colour=[255, 236, 200], amount=0.15),
    wait(0.9),
    pose('npc_vask_porch', ''),
    wait(1.2),
    cam('player', 0.4),
    flag('ACT1_06_VASK_AWAKE'),
    quest('q_act1_gather'),
    *end_control(),
    {'do': 'tip', 'id': 'gathering'},
]})

# ---------------------------------------------------------------------------------------------
#  Scenes 24-26 -- Learn to Gather, in any order
# ---------------------------------------------------------------------------------------------
S.append({'id': 'act1_tut_wood', 'on': {'talk': 'npc_sawyer', 'map': 'town_havenbrook'},
          'when': when(['ACT1_06_VASK_AWAKE'], ['ACT1_TUT_WOODCUTTING_INTRO']), 'steps': [
    bars(True),
    cam(['player', 'npc_sawyer'], 0.6, False),
    narrate('At the edge of the trees, the woodcutter sits against a stump, rubbing their eyes, an axe leaning beside '
            'them. They climb to their feet with a groan.'),
    pose('npc_sawyer', ''),
    face('npc_sawyer', 'player'),
    say('npc_sawyer', "(rolling their shoulders) Felt like the whole forest was coming down on me. Then something cut me "
                      "loose. Don't tell me that was you... no, don't. I'd rather not know."),
    say('npc_sawyer', "Still, a debt's a debt. You've got two good arms and no trade I can see. Let me show you how "
                      "to put a tree down properly."),
    walk('npc_sawyer', 'wood_stand', 58, wait=False),
    walk('player', 'wood_watch', 60),
    {'do': 'wait', 'for': 'all'},
    face('npc_sawyer', 'wood_tree_demo'),
    face('player', 'npc_sawyer'),
    cam(['wood_stand', 'wood_tree_demo'], 0.6, zoom=1.25),
    say('npc_sawyer', 'Feet planted. Eyes on the cut. Let the axe do the work.'),
    pose('npc_sawyer', 'chop'), wait(0.55), sfx('chop', 1.0, 0.85), wait(0.6),
    pose('npc_sawyer', 'chop'), wait(0.55), sfx('chop', 1.0, 0.85), wait(0.6),
    pose('npc_sawyer', 'chop'), wait(0.55), sfx('chop', 1.0, 0.8),
    {'do': 'spawn', 'actor': 'log', 'at': 'wood_tree_demo', 'dx': 10, 'dy': 10},
    sfx('land', 0.6, 1.2),
    pose('npc_sawyer', ''),
    say('npc_sawyer', "Three good hits and she gives up a log. That's all there is to it."),
    {'do': 'remove', 'actor': 'log'},
    face('npc_sawyer', 'player'),
    narrate('The woodcutter reaches behind the stump, pulls out a spare axe and holds it out to you, with a nod at a second '
            'glowing tree.'),
    {'do': 'give', 'item': 'bronze_axe'},
    say('npc_sawyer', 'Now you. Do just what I did.'),
    flag('ACT1_TUT_WOODCUTTING_INTRO'),
    quest('q_act1_tut_wood'),
    *end_control(),
]})
S.append({'id': 'act1_tut_wood_done', 'on': {'flag': 'ACT1_TUT_WOODCUTTING_DONE'},
          'when': when([], ['ACT1_TANNER_QUEST_START']), 'steps': [
    bars(True),
    cam(['player', 'npc_sawyer'], 0.6),
    narrate('The last log thumps into the grass. The woodcutter nods, then frowns at the treeline and rubs the back '
            'of their neck.'),
    say('npc_sawyer', "Clean cuts. You're a natural."),
    say('npc_sawyer', "(frowning) Strange thing, though. All those nights I lay dreaming, I swear I heard wolves, "
                      "circling down by the tannery. There haven't been wolves in these woods in years. Might be "
                      "worth looking in on the Tanner."),
    count('ACT1_LEARN'),
    flag('ACT1_TANNER_QUEST_START'),
    quest('q_act1_hides'),
    *end_control(),
]})

S.append({'id': 'act1_tut_fish', 'on': {'talk': 'npc_angler', 'map': 'town_havenbrook'},
          'when': when(['ACT1_06_VASK_AWAKE'], ['ACT1_TUT_FISHING_INTRO']), 'steps': [
    bars(True),
    cam(['player', 'npc_angler'], 0.6, False),
    narrate("The angler is slumped on an overturned crate at the water's edge, rod across her knees, line tangled "
            "round her boots. She jerks awake, blinking at the water."),
    say('npc_angler', '(muttering) Rising... it was rising... Oh. Dry boots. Dry boots! Ha!'),
    pose('npc_angler', ''),
    face('npc_angler', 'player'),
    say('npc_angler', "Dreamt the water kept climbing and I couldn't get my line out. Then it all just drained away. "
                      "Somebody's done me a kindness. Come on, step up to the edge. The fish don't care who saved "
                      "whom."),
    walk('npc_angler', 'fish_stand', 56, wait=False),
    walk('player', 'fish_watch', 60),
    {'do': 'wait', 'for': 'all'},
    face('npc_angler', 'fish_bobber'),
    face('player', 'fish_bobber'),
    cam(['fish_stand', 'fish_bobber'], 0.6, zoom=1.25),
    say('npc_angler', "Easy does it. No heaving. Flick, don't fling."),
    pose('npc_angler', 'fish'),
    wait(0.7),
    sfx('splash', 0.6, 1.3),
    {'do': 'spawn', 'actor': 'bobber', 'at': 'fish_bobber'},
    wait(1.6),
    # The same two dips the player will fish by (Gathering::Angler): a nibble,
    # and then the bobber pulled under.
    sfx('plop', 0.55, 1.3),
    {'do': 'dip', 'who': 'bobber', 'depth': 3, 'time': 0.45},
    narrate('The bobber dips once.'),
    say('npc_angler', '(softly) Not yet. Wait for the second dip.'),
    wait(1.0),
    sfx('plop', 1.0, 0.8),
    {'do': 'dip', 'who': 'bobber', 'depth': 7, 'time': 0.1, 'hold': True},
    narrate('The bobber dips again.'),
    say('npc_angler', 'Now!'),
    sfx('splash', 1.0, 0.9),
    {'do': 'remove', 'actor': 'bobber'},
    {'do': 'spawn', 'actor': 'fish', 'at': 'fish_stand', 'dx': 14, 'dy': -6, 'lift': 18},
    narrate('A flick of the wrist, and the rod bends. She reels in a silver fish that flashes in the sun, and drops '
            'it into a bucket.'),
    {'do': 'remove', 'actor': 'fish'},
    pose('npc_angler', ''),
    face('npc_angler', 'player'),
    say('npc_angler', 'Patience first, quick hands second. Now you try. Do what I did.'),
    narrate('She presses a spare rod into your hands.'),
    {'do': 'give', 'item': 'fishing_rod'},
    flag('ACT1_TUT_FISHING_INTRO'),
    quest('q_act1_tut_fish'),
    *end_control(),
    {'do': 'tip', 'id': 'fishing'},
]})
S.append({'id': 'act1_tut_fish_done', 'on': {'flag': 'ACT1_TUT_FISHING_DONE'},
          'when': when([], ['ACT1_TUT_FISHING_SAID']), 'steps': [
    bars(True),
    cam(['player', 'npc_angler'], 0.6),
    narrate('The third fish flops into the bucket. The angler grins and wipes her hands on her trousers.'),
    say('npc_angler', "Patience first, quick hands second, and you've got both. You'll do fine by the water."),
    say('npc_angler', "(glancing upriver) Funny thing, though. The river's running colder than it should this time "
                      "of year. Never seen it do that."),
    count('ACT1_LEARN'),
    flag('ACT1_TUT_FISHING_SAID'),
    *end_control(),
]})

S.append({'id': 'act1_tut_mine', 'on': {'talk': 'npc_pitmaster', 'map': 'town_havenbrook'},
          'when': when(['ACT1_06_VASK_AWAKE'], ['ACT1_TUT_MINING_INTRO']), 'steps': [
    bars(True),
    cam(['player', 'npc_pitmaster'], 0.6, False),
    pose('npc_pitmaster', ''),
    narrate('The miner stumbles out from under the pit face, helmet askew, pickaxe in hand, squinting into the '
            'daylight.'),
    say('npc_pitmaster', "(squinting) Sun... real sun. It was so dark down there. Then somebody let a little air in, "
                         "and I could breathe again."),
    face('npc_pitmaster', 'player'),
    narrate('The miner looks at you, then up at the sky, and laughs.'),
    say('npc_pitmaster', "I'm no good with speeches. Let me pay you back the only way I know how: by teaching you to "
                         "read a rock."),
    walk('npc_pitmaster', 'mine_stand', 56, wait=False),
    walk('player', 'mine_watch', 60),
    {'do': 'wait', 'for': 'all'},
    face('npc_pitmaster', 'mine_rock_demo'),
    face('player', 'npc_pitmaster'),
    cam(['mine_stand', 'mine_rock_demo'], 0.6, zoom=1.25),
    say('npc_pitmaster', "See that glint? That's the seam. Find the glint, strike the seam, and let the rock come "
                         "to you."),
    pose('npc_pitmaster', 'mine'), wait(0.5), sfx('mine', 0.8, 1.2), wait(0.7),
    pose('npc_pitmaster', 'mine'), wait(0.5), sfx('mine', 0.8, 1.25),
    narrate('A hairline crack races across the rock.'),
    pose('npc_pitmaster', 'mine'), wait(0.55), sfx('mine', 1.0, 0.75),
    {'do': 'spawn', 'actor': 'ore', 'at': 'mine_rock_demo', 'dx': -8, 'dy': 12},
    pose('npc_pitmaster', ''),
    say('npc_pitmaster', "Two taps to find the crack, one hard strike to finish it. Don't fight the stone."),
    {'do': 'remove', 'actor': 'ore'},
    face('npc_pitmaster', 'player'),
    narrate('The miner scoops up the ore and passes you a spare pickaxe.'),
    {'do': 'give', 'item': 'bronze_pickaxe'},
    say('npc_pitmaster', 'Now you. Do just what I did.'),
    flag('ACT1_TUT_MINING_INTRO'),
    quest('q_act1_tut_mine'),
    *end_control(),
]})
S.append({'id': 'act1_tut_mine_done', 'on': {'flag': 'ACT1_TUT_MINING_DONE'},
          'when': when([], ['ACT1_TUT_MINING_SAID']), 'steps': [
    bars(True),
    cam(['player', 'npc_pitmaster'], 0.6),
    narrate('The third chunk of ore thuds into your pack. The miner whistles and tips back their helmet.'),
    say('npc_pitmaster', 'Two taps, one hard strike. You listened. Most folks fight the stone.'),
    say('npc_pitmaster', "(clapping you on the shoulder) Come find me when you want to learn which seams are worth the "
                         "sweat. I'll teach you to read anything that glints."),
    count('ACT1_LEARN'),
    flag('ACT1_TUT_MINING_SAID'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scenes 27-29 -- Dawn Chimes, ten of them
# ---------------------------------------------------------------------------------------------
CHIMES = [('posy', 'npc_posy'), ('tobin', 'npc_tobin'), ('ivo', 'npc_hunter'), ('marrow', 'npc_marrow'),
          ('wenna', 'npc_wenna'), ('perrin', 'npc_perrin'), ('pip', 'npc_pip'), ('bram', 'npc_crier'),
          ('hester', 'npc_hester'), ('hollis', 'npc_carter')]
S.append({'id': 'act1_chimes_tip', 'on': {'flag': 'ACT1_06_VASK_AWAKE'}, 'when': when([], ['ACT1_TIP_CHIMES']),
          'steps': [flag('ACT1_TIP_CHIMES'), {'do': 'tip', 'id': 'dawn_chimes'}]})
for key, npc in CHIMES:
    up = key.upper()
    dream = 'chime_' + key
    done = 'SIDE_CHIMES_DONE_' + up
    S.append({'id': 'chime_%s_catch' % key, 'on': {'talk': npc, 'map': 'town_havenbrook'},
              'needs': 'vigil_dreamcatcher', 'when': when(['ECHO_HAVENBROOK_BELLS'], [done, 'ECHO_HAVENBROOK_FINAL_BELL']),
              'steps': [
        *threads('The Dreamcatcher hums against your side. You lift it. Fine threads of pale light drift from the '
                 "sleeper's chest into the hoop, thinner and quicker than they did with Elder Vask. The web spins "
                 'tight.'),
        quest('q_chime_' + key),
        flag('CHIME_%s_ENTERED' % up),
        fx('dream', map=dream, spawn='arrival', story=True),
        wait(1.4),
        bars(False),
    ]})
    S.append({'id': 'chime_%s_arrive' % key, 'on': {'enter': dream},
              'when': when(['CHIME_%s_ENTERED' % up], ['CHIME_%s_GO' % up]), 'steps': [
        bars(True, 0.0),
        music('dream', 2.0),
        cam('anchor_at', zoom=1.1),
        wait(0.8),
        narrate("The sleeper's home, as they dream it: furniture stacked too high, the walls swaying like curtains. "
                "In the middle of the room pulses a small knot of black thread no taller than a person. Threads run "
                "from it to the sleeper's dream-self, who cowers beneath it. Hushed ring the knot, humming."),
        narrate('The Hushed peel out of the walls and the floor.'),
        flag('CHIME_%s_GO' % up),
        *end_control(),
    ]})
    S.append({'id': 'chime_%s_again' % key, 'on': {'enter': dream},
              'when': when(['CHIME_%s_GO' % up], [done]), 'steps': [music('dream', 2.0)]})
    S.append({'id': 'chime_%s_round2' % key, 'on': {'flag': 'CHIME_%s_R_1' % up},
              'when': when([], ['CHIME_%s_R1_SAID' % up]), 'steps': [
        flag('CHIME_%s_R1_SAID' % up),
        narrate('The knot loosens a notch. The humming rises, and a second wave closes in.'),
    ]})
    S.append({'id': 'chime_%s_round3' % key, 'on': {'flag': 'CHIME_%s_R_2' % up},
              'when': when([], ['CHIME_%s_R2_SAID' % up]), 'steps': [
        flag('CHIME_%s_R2_SAID' % up),
        narrate("The knot shudders, and the last and largest wave pours out of the room's shadows."),
    ]})
    S.append({'id': 'chime_%s_unravel' % key, 'on': {'flag': 'CHIME_%s_R_3' % up},
              'when': when([], ['CHIME_%s_R3_SAID' % up]), 'steps': [
        bars(True),
        cam('anchor_at', 0.5),
        fx('shatter', at='anchor_at', radius=50, amount=0.3),
        flag('CHIME_%s_R3_SAID' % up),
        narrate('With the last Hushed gone, the knot slackens and unravels. Where it hung, a short string of pale '
                'chimes drifts in the air.'),
        *end_control(),
    ]})
    S.append({'id': 'chime_%s_ring' % key, 'on': {'use': dream + '_chimes', 'map': dream},
              'when': when(['CHIME_%s_R_3' % up], [done]), 'steps': [
        bars(True),
        cam('anchor_at', 0.4),
        sfx('chime', 1.0),
        narrate('A high, clear chime, much smaller than the Dawn Bells.'),
        pose('npc_dreamself_' + key, ''),
        narrate("The sleeper's dream-self lifts its head, smiles, and dissolves into light."),
        fx('vanish', who='npc_dreamself_' + key, sink=False),
        wait(1.0),
        flag(done),
        white_out(0.5),
        music('', 0.6),
        wake('town_havenbrook', npc, dx=30, dy=6),
        face('player', npc),
        cam(['player', npc]),
        wait(0.5),
        fade('clear', 1.4),
        sfx('chime', 0.25),
        say(npc, "(groggy) Whoa... did I fall asleep standing up? Strange dream. Something kept humming at me, and "
                 "then a bell rang and it stopped. Here. You look like you could use this."),
        flag('CHIME_%s_THANKED' % up),
        *end_control(),
    ]})

# The last bell wakes everybody, and so whoever's dream was begun and not
# finished: their Dawn Chime is done, and its quest with it (no sleeper is left
# to go back into).
for key, npc in CHIMES:
    up = key.upper()
    S.append({'id': 'chime_%s_woken' % key, 'on': {'flag': 'ECHO_HAVENBROOK_FINAL_BELL'},
              'when': when(['CHIME_%s_ENTERED' % up], ['SIDE_CHIMES_DONE_' + up]),
              'steps': [*[flag('CHIME_%s_R_%d' % (up, r)) for r in (1, 2, 3)],
                        flag('SIDE_CHIMES_DONE_' + up), flag('CHIME_%s_THANKED' % up)]})

# ---------------------------------------------------------------------------------------------
#  Scenes 30-35 -- Halda
# ---------------------------------------------------------------------------------------------
S.append({'id': 'act1_forge_notice', 'on': {'near': 'forge_look', 'map': 'town_havenbrook', 'radius': 150},
          'when': when(['ACT1_06_VASK_AWAKE'], ['ACT1_FORGE_QUEST_START']), 'steps': [
    bars(True),
    cam('forge_chimney', 0.9),
    narrate('The forge stands at the edge of the market, built of heavy stone and dark timber, an iron anvil hanging '
            'above its door. No smoke billows from its chimney. A forge is never meant to be cold.'),
    narrate('The door is shut. No hammer rings inside, and the quiet where it should be feels wrong.'),
    flag('ACT1_FORGE_QUEST_START'),
    quest('q_act1_forge'),
    *end_control(),
]})
S.append({'id': 'act1_forge_enter', 'on': {'enter': 'house_smith'},
          'when': when(['ACT1_06_VASK_AWAKE'], ['ACT1_FORGE_ENTERED']), 'steps': [
    bars(True, 0.0),
    flag('ACT1_FORGE_QUEST_START'),
    quest('q_act1_forge'),
    cam('forge_cam', zoom=1.1),
    wait(0.6),
    narrate('The forge is dim. The furnace burns a dull violet instead of orange, throwing cold light across the '
            'anvil, the racks of half-finished blades and the scorched floor.'),
    narrate('In the centre of the room, Halda hangs slumped in a heavy chair, bound by black chains that run up into '
            'the rafters. Her head is down. She does not move. Four Black Knights stand guard around her in black '
            'plate, their visors glowing faintly violet.'),
    sfx('grind', 0.9),
    narrate('As you cross the threshold, the knights turn their helmets in unison. Swords scrape from their '
            'scabbards.'),
    flag('ACT1_FORGE_ENTERED'),
    music('ominous', 1.0),
    *end_control(),
]})
S.append({'id': 'act1_forge_knights_down', 'on': {'flag': 'ACT1_FORGE_KNIGHTS_DOWN'},
          'when': when([], ['ACT1_FORGE_CHAINS']), 'steps': [
    bars(True),
    cam('forge_chair', 0.6),
    sfx('impact', 0.7, 0.6),
    flag('ACT1_FORGE_CHAINS'),
    narrate('As the last knight drops, the chains around Halda go slack and clatter to the floor. The furnace gutters '
            'from violet to a weak, ember orange. Silence.'),
    music('', 2.0),
    *end_control(),
]})
wake_tries('ACT1_HALDA', 'npc_smith', 'house_smith',
           ['You shake her shoulder. Her head rolls limply.',
            'You call her name. Nothing.',
            'Her hands are clenched around something that isn\'t there, as if she is gripping a hammer in her sleep.'],
           'ACT1_HALDA_STILL_DREAMING', ['ACT1_FORGE_KNIGHTS_DOWN'],
           [sfx('chime', 0.3, 0.7),
            narrate('The Dreamcatcher hums against your side and warms, tugging toward Halda like a compass needle. '
                    'The knights were only her jailers. Halda is still in the Reverie.')])
S.append({'id': 'act1_halda_catch', 'on': {'talk': 'npc_smith', 'map': 'house_smith'},
          'needs': 'vigil_dreamcatcher', 'when': when(['ACT1_HALDA_STILL_DREAMING'], ['ECHO_HALDA_FORGE_RELIT']),
          'steps': [
    *threads("Threads of pale light drift from Halda's chest into the hoop. The web spins and pulls you in."),
    flag('ACT1_HALDA_DREAM_ENTERED'),
    fx('dream', map='dream_forge', spawn='arrival', story=True),
    wait(1.4),
    bars(False),
]})
S.append({'id': 'act1_forge_dream', 'on': {'enter': 'dream_forge'},
          'when': when(['ACT1_HALDA_DREAM_ENTERED'], ['ACT1_FORGE_DREAM_INTRO']), 'steps': [
    bars(True, 0.0),
    music('dream', 2.0),
    cam('forge_platform', zoom=0.85),
    wait(1.0),
    narrate("Halda's dream of her forge: a vast, cavernous hall. Rivers of molten metal run where the water troughs "
            "should be. Anvils the size of cottages squat in the dark, and giant hammers hang from the rafters like "
            "silent bells."),
    narrate('A causeway of stone slabs crosses the lava to a wide platform at the centre. There, Halda fights alone, '
            'swinging a heavy smith\'s hammer at a colossal figure: the Forge Demon, a hulking giant of blackened '
            'armour split by glowing seams of molten metal.'),
    pose('npc_halda_dream', 'swing'),
    sfx('anvil', 0.6, 1.3),
    narrate('A thick black thread binds her ankle to the great anvil at the platform\'s centre. She can circle it, '
            'but she cannot leave. Her hammer rings off the demon\'s armour in a shower of sparks. It barely notices.'),
    cam(['player', 'forge_platform'], 0.8),
    face('npc_halda_dream', 'player'),
    say('npc_halda_dream', "(shouting over the roar) Don't just stand there! I've been swinging at this thing for what "
                           "feels like days, and it hasn't even slowed down! There's a causeway across the slag. Get "
                           "over here, and I'll keep its eye on me!"),
    flag('ACT1_FORGE_DREAM_INTRO'),
    *end_control(),
    {'do': 'tip', 'id': 'story_dream'},
]})
S.append({'id': 'act1_forge_dream_boss_again', 'on': {'enter': 'dream_forge'},
          'when': when(['ACT1_HALDA_DEMON_START'], ['ACT1_HALDA_DEMON_DOWN']), 'steps': [music('boss', 1.0)]})
S.append({'id': 'act1_forge_dream_again', 'on': {'enter': 'dream_forge'},
          'when': when(['ACT1_FORGE_DREAM_INTRO'], ['ECHO_HALDA_FORGE_RELIT']), 'steps': [music('dream', 2.0)]})
S.append({'id': 'act1_platform', 'on': {'near': 'forge_platform', 'map': 'dream_forge', 'radius': 110},
          'when': when(['ACT1_FORGE_DREAM_INTRO'], ['ACT1_HALDA_DEMON_START']), 'steps': [
    bars(True),
    cam(['player', 'forge_platform'], 0.5),
    sfx('roar', 1.0, 0.7),
    narrate('The Forge Demon turns away from Halda and toward you, with a roar that shakes dust from the hanging '
            'hammers.'),
    flag('ACT1_HALDA_DEMON_START'),
    music('boss', 0.5),
    say('npc_halda_dream', "That's it, over here! Hit it where it glows! The seams are soft!"),
    *end_control(),
    {'do': 'tip', 'id': 'weak_seams'},
]})
S.append({'id': 'act1_demon_down', 'on': {'flag': 'ACT1_HALDA_DEMON_DOWN'},
          'when': when([], ['ACT1_DEMON_FALL_SAID']), 'steps': [
    bars(True),
    wait(1.0),
    music('dream', 2.0),
    narrate('The demon drops to one knee. Its fire gutters. It topples, and the platform shakes. The armour cracks '
            'apart and falls into the lava in pieces, hissing.'),
    narrate("The black thread around Halda's ankle goes slack. At the centre of the platform, the great anvil is "
            'wrapped in a knot of black thread: the Anchor.'),
    flag('ACT1_DEMON_FALL_SAID'),
    *end_control(),
]})
S.append({'id': 'act1_forge_anchor_held', 'on': {'use': 'forge_anchor', 'map': 'dream_forge'},
          'when': when([], ['ACT1_HALDA_DEMON_DOWN']),
          'steps': [narrate('The Anchor cannot be touched while the demon stands.')]})
S.append({'id': 'act1_forge_anchor', 'on': {'use': 'forge_anchor', 'map': 'dream_forge'},
          'when': when(['ACT1_HALDA_DEMON_DOWN'], ['ECHO_HALDA_FORGE_RELIT']), 'steps': [
    bars(True),
    cam('forge_platform', 0.5),
    narrate('You break the Anchor. The thread snaps and the knot unravels. The lava cools to a dull red, and the '
            'furnace flares from violet to a clean orange.'),
    fx('shatter', at='forge_anchor_at', radius=80),
    flag('ACT1_FORGE_ANCHOR_BROKEN'),
    pose('npc_halda_dream', 'slump'),
    narrate('Halda sinks onto one knee, leaning on her hammer.'),
    say('npc_halda_dream', "(breathing hard) Never thought I'd be glad to see a stranger in my forge. Thank you."),
    narrate('She looks around at the cooling lava and the hanging hammers.'),
    say('npc_halda_dream', "When I'm back on my feet, come by the forge. I owe you some work."),
    sfx('anvil', 1.0),
    narrate('One clean anvil strike rolls out across the dream forge, deep and ringing.'),
    flag('ECHO_HALDA_FORGE_RELIT'),
    white_out(0.5),
    music('', 0.6),
    wake('house_smith', 'halda_wake'),
    face('player', 'npc_smith'),
    cam('forge_chair'),
    wait(0.5),
    fade('clear', 1.4),
    sfx('anvil', 0.25),
    narrate('The forge in Solace. The furnace burns a warm orange, and the black chains are gone. A last ring fades '
            'from the anvil.'),
    pose('npc_smith', ''),
    say('npc_smith', "(rubbing her wrists) My arms feel like I swung a hammer for a week. Ha. Don't answer that."),
    narrate('She stands, rolls her shoulders, and picks a hammer up from the anvil.'),
    walk('npc_smith', 'halda_anvil', 50),
    face('npc_smith', 'player'),
    say('npc_smith', 'Now. I believe I owe you some work. Bring me something that needs mending, or something that '
                     'needs making.'),
    flag('ACT1_HALDA_AWAKE'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scenes 36-40 -- Bess
# ---------------------------------------------------------------------------------------------
S.append({'id': 'act1_bess_start', 'on': {'enter': 'house_inn'},
          'when': when(['ACT1_06_VASK_AWAKE'], ['ACT1_BESS_QUEST_START']), 'steps': [
    bars(True, 0.0),
    cam('inn_cam', zoom=1.05),
    wait(0.5),
    narrate("The tavern is as you left it, only emptier. Stools lie overturned. A half-full mug sits on the bar, and "
            "Bess's apron hangs from its hook. Bess herself is nowhere. The hearth is cold, and the kitchen is silent."),
    cam('inn_cellar_lock_at', 0.8),
    narrate('Behind the bar stands a heavy cellar door, shut and locked. A cold draft creeps from beneath it.'),
    flag('ACT1_BESS_QUEST_START'),
    quest('q_act1_bess'),
    *end_control(),
]})
S.append({'id': 'act1_inn_lock_first', 'on': {'use': 'inn_cellar_lock', 'map': 'house_inn'},
          'when': when(['ACT1_BESS_QUEST_START'], ['ACT1_INN_LOCK_SEEN']), 'steps': [
    narrate('The cellar door is locked. The lock has three round slots.'),
    {'do': 'note', 'title': 'The lock', 'text': 'Three brass slots, each stamped with a symbol: a candle, a loaf, '
                                                'a mug.'},
    flag('ACT1_INN_LOCK_SEEN'),
]})
S.append({'id': 'act1_inn_lock_wait', 'on': {'use': 'inn_cellar_lock', 'map': 'house_inn'},
          'when': when(['ACT1_INN_LOCK_SEEN'], ['ACT1_INN_TOKENS_FOUND_3', 'ACT1_INN_CELLAR_OPEN']), 'steps': [
    narrate('Three brass slots: a candle, a loaf, a mug. Something somewhere in the inn must fit them.'),
]})
S.append({'id': 'act1_inn_lock_try', 'on': {'use': 'inn_cellar_lock', 'map': 'house_inn'},
          'when': when(['ACT1_INN_LOCK_SEEN', 'ACT1_INN_TOKENS_FOUND_3'], ['ACT1_INN_CELLAR_OPEN']), 'steps': [
    {'do': 'dialogue', 'id': 'inn_lock_1', 'title': 'Cellar Door'},
]})
S.append({'id': 'act1_inn_open', 'on': {'flag': 'ACT1_INN_CELLAR_OPEN'},
          'when': when([], ['ACT1_INN_DOOR_SAID']), 'steps': [
    flag('ACT1_INN_DOOR_SAID'),
    {'do': 'take', 'item': 'token_candle'}, {'do': 'take', 'item': 'token_loaf'}, {'do': 'take', 'item': 'token_mug'},
    sfx('door', 1.0, 0.8),
    narrate('The door swings open on a cold draft and a dark stair. Black webbing clings to the steps, thicker the '
            'lower they go.'),
]})
TOKENS = [
    ('loaf', 'inn_loaf', 'house_inn', 'A stale loaf sits on the cooling rack.',
     'You break the stale loaf open. A brass token stamped with a loaf falls out.'),
    ('mug', 'inn_mug', 'house_inn', 'A half-full mug, left on the bar.',
     'You tip out the mug. A brass token stamped with a mug clinks onto the wood.'),
    ('candle', 'inn_candlestick', 'house_inn_upper', 'A candlestick on the bedside table.',
     'You lift the candlestick. A brass token stamped with a candle sits beneath its base.'),
]
for key, obj, room, idle, found in TOKENS:
    S.append({'id': 'act1_token_%s' % key, 'on': {'use': obj, 'map': room},
              'when': when(['ACT1_BESS_QUEST_START'], ['ACT1_TOKEN_%s' % key.upper()]), 'steps': [
        narrate(found),
        sfx('pickup', 0.8),
        {'do': 'give', 'item': 'token_' + key},
        flag('ACT1_TOKEN_%s' % key.upper()),
        count('ACT1_INN_TOKENS_FOUND'),
    ]})
    S.append({'id': 'act1_token_%s_idle' % key, 'on': {'use': obj, 'map': room},
              'when': when([], ['ACT1_BESS_QUEST_START']), 'steps': [narrate(idle)]})
S.append({'id': 'act1_sampler', 'on': {'use': 'inn_sampler', 'map': 'house_inn'}, 'steps': [
    {'do': 'note', 'title': 'A sampler over the hearth',
     'text': 'Cross-stitched in a careful hand:\n\nRise with the flame,\nbreak bread by noon,\nraise a mug by the stars.'},
]})
S.append({'id': 'act1_cellar_enter', 'on': {'enter': 'house_inn_cellar'},
          'when': when(['ACT1_INN_CELLAR_OPEN'], ['ACT1_CELLAR_SEEN']), 'steps': [
    bars(True, 0.0),
    cam('cellar_cam'),
    wait(0.5),
    narrate('The cellar: casks stacked to the low ceiling, shelves of bottles and sacks, black webbing over '
            'everything. Something skitters in the dark.'),
    flag('ACT1_CELLAR_SEEN'),
    music('ominous', 1.0),
    narrate('Nightmare spiders drop from the rafters, from palm-sized swarmers to a few as big as hounds.'),
    *end_control(),
]})
S.append({'id': 'act1_cellar_cleared', 'on': {'flag': 'ACT1_INN_CELLAR_CLEARED'},
          'when': when([], ['ACT1_CELLAR_QUIET']), 'steps': [
    bars(True),
    music('', 2.0),
    cam('npc_bess_cellar', 0.7),
    narrate('When the last one falls, the cellar goes quiet. At the foot of the stairs, Bess lies slumped against a '
            'cask, tangled in webbing, eyes shut. A thread of web runs from her wrist into the dark.'),
    flag('ACT1_CELLAR_QUIET'),
    *end_control(),
]})
wake_tries('ACT1_BESS', 'npc_bess_cellar', 'house_inn_cellar',
           ["You brush the webbing from her face. She doesn't stir.",
            "You say her name. Her lips move, as if she's counting something.",
            "Her fingers twitch against the cask, as if she's stacking something that keeps falling over."],
           'ACT1_BESS_STILL_DREAMING', ['ACT1_INN_CELLAR_CLEARED'],
           [sfx('chime', 0.3, 0.7), narrate('The Dreamcatcher hums against your side and warms.')])
S.append({'id': 'act1_bess_catch', 'on': {'talk': 'npc_bess_cellar', 'map': 'house_inn_cellar'},
          'needs': 'vigil_dreamcatcher', 'when': when(['ACT1_BESS_STILL_DREAMING'], ['ECHO_BESS_CHIME']), 'steps': [
    *threads("Threads of pale light drift from Bess's chest into the hoop. The web spins and pulls you in."),
    flag('ACT1_BESS_DREAM_ENTERED'),
    fx('dream', map='dream_cellar', spawn='arrival', story=True),
    wait(1.4),
    bars(False),
]})
S.append({'id': 'act1_bess_dream', 'on': {'enter': 'dream_cellar'},
          'when': when(['ACT1_BESS_DREAM_ENTERED'], ['ACT1_BESS_DREAM_INTRO']), 'steps': [
    bars(True, 0.0),
    music('dream', 2.0),
    cam('bess_dream_cam', zoom=1.05),
    wait(0.8),
    narrate('The cellar as Bess dreams it: casks stacked impossibly high, shelves sagging under bottles and sacks '
            'that spill and spill again, black webbing swallowing everything. Pale-bellied spiders skitter over the '
            'barrels, spinning and spinning.'),
    narrate('In the centre, Bess\'s dream-self is bound upright against a cask in webbing. Above her, from a rafter '
            'in a thicket of web, hangs a small, tarnished bronze chime that glows faintly gold: the Dawn Chime.'),
    say('npc_bess_dream', "(muffled, struggling against the web) Don't just stand there gawking! They've been "
                          "spinning over my stores for days, and every time I stack a barrel, down it comes! Clear "
                          "them out, and ring that chime. It's the only thing that'll end this!"),
    narrate('The spiders turn toward you.'),
    flag('ACT1_BESS_DREAM_INTRO'),
    music('boss', 0.8),
    *end_control(),
    {'do': 'tip', 'id': 'story_dream'},
]})
S.append({'id': 'act1_bess_dream_again', 'on': {'enter': 'dream_cellar'},
          'when': when(['ACT1_BESS_DREAM_INTRO'], ['ECHO_BESS_CHIME']), 'steps': [music('dream', 2.0)]})
S.append({'id': 'act1_bess_spiders_down', 'on': {'flag': 'ACT1_BESS_SPIDERS_DOWN'},
          'when': when([], ['ACT1_BESS_WEB_SLACK']), 'steps': [
    flag('ACT1_BESS_WEB_SLACK'),
    music('dream', 2.0),
    narrate('When the last spider falls, the webbing around the chime goes slack.'),
]})
S.append({'id': 'act1_bess_chime_held', 'on': {'use': 'bess_chime', 'map': 'dream_cellar'},
          'when': when(['ACT1_BESS_DREAM_INTRO'], ['ACT1_BESS_SPIDERS_DOWN']),
          'steps': [narrate('The chime hangs in a thicket of web, out of reach while the spiders spin.')]})
S.append({'id': 'act1_bess_chime', 'on': {'use': 'bess_chime', 'map': 'dream_cellar'},
          'when': when(['ACT1_BESS_SPIDERS_DOWN'], ['ECHO_BESS_CHIME']), 'steps': [
    bars(True),
    cam('bess_dream_cam', 0.5),
    sfx('chime', 1.0),
    narrate('A clear, sweet chime rolls out across the cellar.'),
    fx('golden', at='bess_dream_cam', amount=0.6),
    narrate("The webs dissolve into drifting dust. The barrels settle upright and stay there. Bess's dream-self gasps, "
            'free of the web, and smiles.'),
    flag('ECHO_BESS_CHIME'),
    white_out(0.5),
    music('', 0.6),
    wake('house_inn_cellar', 'bess_wake'),
    face('player', 'npc_bess_cellar'),
    cam('npc_bess_cellar'),
    wait(0.5),
    fade('clear', 1.4),
    narrate('The black webbing in the cellar falls away in drifting dust. Bess blinks awake and struggles up from the '
            'cask.'),
    pose('npc_bess_cellar', ''),
    say('npc_bess_cellar', '(groggy, then alarmed) The barrels! Did they... Oh. It was a dream. Wasn\'t it?'),
    face('npc_bess_cellar', 'player'),
    narrate('She sees you. A long beat. She brushes the last cobwebs from her apron.'),
    say('npc_bess_cellar', 'You came down here. For me.'),
    narrate('She takes your hand and shakes it hard.'),
    say('npc_bess_cellar', "Well. I've fed half this town on credit, and I've never owed anyone what I owe you. "
                           "Whenever you're hungry, there's a hot meal at my bar with your name on it. A proper one, "
                           "not wrapped scraps. No arguments."),
    walk('npc_bess_cellar', 'cellar_stairs', 60),
    flag('ACT1_BESS_AWAKE'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scenes 41-43 -- the Tanner
# ---------------------------------------------------------------------------------------------
S.append({'id': 'act1_tannery', 'on': {'near': 'tannery_yard', 'map': 'town_havenbrook', 'radius': 130},
          'when': when(['ACT1_TANNER_QUEST_START'], ['ACT1_TANNER_SEEN']), 'steps': [
    bars(True),
    cam('tannery_yard', 0.8),
    narrate('The tannery sits at the edge of town, downwind of everything: a yard of stretched hides on drying '
            'frames, vats of dark liquid, and a lean-to workshop. There is no wind, yet the hides sway.'),
    narrate("A patch of frost glitters on the yard's stones in broad daylight, and from somewhere unseen, a single "
            'wolf howls.'),
    sfx('howl', 0.45, 1.05),
    wait(1.2),
    narrate('Deep claw marks score the doorframe and the workbench, as though something has been trying to get in. '
            'Rows of wolf pelts hang from the racks, their empty eyeholes seeming to follow you.'),
    cam('npc_nessa', 0.6),
    narrate('At the bench sits the Tanner, bolt upright with a skinning knife clenched in her fist. She is asleep. '
            'Her head jerks, and her whole body flinches.'),
    say('npc_nessa', "(muttering, eyes shut) Not mine... they weren't mine... I only took what I needed..."),
    flag('ACT1_TANNER_SEEN'),
    *end_control(),
]})
wake_tries('ACT1_TANNER', 'npc_nessa', 'town_havenbrook',
           ['You touch her shoulder. She flinches as if struck.',
            'You call out to her. Her knife hand tightens.',
            'Her breath fogs in the warm air, as if she were standing in a winter wood.'],
           'ACT1_TANNER_STILL_DREAMING', ['ACT1_TANNER_SEEN'],
           [sfx('chime', 0.3, 0.7), narrate('The Dreamcatcher hums, and the wolf pelts on the racks tremble.')])
S.append({'id': 'act1_tanner_catch', 'on': {'talk': 'npc_nessa', 'map': 'town_havenbrook'},
          'needs': 'vigil_dreamcatcher', 'when': when(['ACT1_TANNER_STILL_DREAMING'], ['ECHO_TANNER_HOWL_FADES']),
          'steps': [
    *threads("Threads of pale light drift from the Tanner's chest into the hoop. The web spins and pulls you in."),
    flag('ACT1_TANNER_DREAM_ENTERED'),
    fx('dream', map='dream_tannery', spawn='arrival', story=True),
    wait(1.4),
    bars(False),
]})
S.append({'id': 'act1_tanner_dream', 'on': {'enter': 'dream_tannery'},
          'when': when(['ACT1_TANNER_DREAM_ENTERED'], ['ACT1_TANNER_DREAM_INTRO']), 'steps': [
    bars(True, 0.0),
    music('dream', 2.0),
    cam('tannery_post_cam', zoom=0.95),
    wait(0.8),
    narrate('The tannery as the Tanner dreams it: a vast, moonless winter yard thick with frost. Drying frames tall as '
            'ship masts hang with hides that sway like banners, and the vats steam in the cold.'),
    narrate("In the centre, the Tanner's dream-self stands with her back against a post, skinning knife drawn. A thick "
            'black thread binds her belt to the post: the Anchor. Around her circle four nightmare wolves, gaunt and '
            'pale-eyed, built from shadow and patchwork pelt, their breath steaming cold.'),
    say('npc_nessa_dream', '(to the wolves) Back! I told you, I only took what I needed!'),
    face('npc_nessa_dream', 'player'),
    say('npc_nessa_dream', "You! Don't come closer... there's four of them. Every pelt I ever hung, and they've come "
                           "back to take it out of me!"),
    narrate('The wolves turn their hollow eyes on you.'),
    flag('ACT1_TANNER_DREAM_INTRO'),
    music('boss', 0.8),
    *end_control(),
    {'do': 'tip', 'id': 'story_dream'},
]})
S.append({'id': 'act1_tanner_dream_again', 'on': {'enter': 'dream_tannery'},
          'when': when(['ACT1_TANNER_DREAM_INTRO'], ['ECHO_TANNER_HOWL_FADES']), 'steps': [music('dream', 2.0)]})
S.append({'id': 'act1_wolves_down', 'on': {'flag': 'ACT1_TANNER_WOLVES_DOWN'},
          'when': when([], ['ACT1_WOLVES_SAID']), 'steps': [
    flag('ACT1_WOLVES_SAID'),
    music('dream', 2.0),
    narrate("As the fourth wolf falls, what is left of the pack dissolves into drifting frost. The black thread around "
            "the Tanner's belt goes slack."),
]})
S.append({'id': 'act1_tannery_anchor_held', 'on': {'use': 'tannery_anchor', 'map': 'dream_tannery'},
          'when': when(['ACT1_TANNER_DREAM_INTRO'], ['ACT1_TANNER_WOLVES_DOWN']),
          'steps': [narrate('The Anchor cannot be broken while the wolves still circle.')]})
S.append({'id': 'act1_tannery_anchor', 'on': {'use': 'tannery_anchor', 'map': 'dream_tannery'},
          'when': when(['ACT1_TANNER_WOLVES_DOWN'], ['ECHO_TANNER_HOWL_FADES']), 'steps': [
    bars(True),
    cam('tannery_post_cam', 0.5),
    narrate('You break the Anchor. The thread snaps, the frost melts, and the hung hides settle and fall still.'),
    fx('shatter', at='tannery_anchor_at', radius=60, amount=0.4),
    flag('ACT1_TANNERY_ANCHOR_BROKEN'),
    sfx('howl', 0.9),
    narrate('A long, mournful wolf howl rolls across the yard and fades into silence.'),
    wait(1.4),
    say('npc_nessa_dream', "(quietly) They're gone. Finally."),
    flag('ECHO_TANNER_HOWL_FADES'),
    white_out(0.5),
    music('', 0.6),
    wake('town_havenbrook', 'nessa_front'),
    face('player', 'npc_nessa'),
    cam('npc_nessa'),
    wait(0.5),
    fade('clear', 1.4),
    narrate('The hides hang perfectly still. The Tanner jerks awake, and her knife clatters onto the bench. Her breath '
            'no longer fogs.'),
    sfx('impact', 0.5, 1.4),
    pose('npc_nessa', ''),
    say('npc_nessa', "(rasping) Gone... they're gone."),
    face('npc_nessa', 'player'),
    narrate('She looks up and sees you. A long beat. She lets out a shaky breath.'),
    say('npc_nessa', "You were there. I don't know how, but you were there. Thank you. I'd never have woken up."),
    narrate('Her eyes drift to the wolf pelts on the racks, then back to you.'),
    say('npc_nessa', "Anything you need cured or tanned, it's on me."),
    flag('ACT1_TANNER_AWAKE'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scenes 43A-43F -- the Clothier
# ---------------------------------------------------------------------------------------------
THREADS = [('crimson', 'BODICE', 'bodice'), ('ivory', 'SKIRT', 'skirt'), ('gold', 'SASH', 'sash')]
SPARES = ['cobalt', 'moss', 'violet']
# The hook: snipping from the shuttered shop, though the town is asleep.
S.append({'id': 'act1_clothier_hook', 'on': {'near': 'clothier_door', 'map': 'town_havenbrook', 'radius': 96},
          'when': when(['ACT1_06_VASK_AWAKE'], ['ACT1_CLOTHIER_QUEST_START']), 'steps': [
    bars(True),
    sfx('snip', 0.35, 1.0),
    wait(0.45),
    sfx('snip', 0.35, 1.05),
    wait(0.45),
    narrate("From the clothier's shuttered shop comes a faint, steady snipping, though the whole town is asleep."),
    sfx('snip', 0.35, 0.95),
    narrate('You try the door. It is unlocked.'),
    flag('ACT1_CLOTHIER_QUEST_START'),
    quest('q_act1_clothier'),
    *end_control(),
]})
S.append({'id': 'act1_clothier_shop', 'on': {'enter': 'clothier_havenbrook'},
          'when': when(['ACT1_CLOTHIER_QUEST_START'], ['ACT1_CLOTHIER_SEEN']), 'steps': [
    bars(True, 0.0),
    cam('clothier_cam', zoom=1.0),
    wait(0.5),
    narrate('Spools of thread in every colour line the wall in long, taut strands. A bare dress form stands in the '
            'middle of the room. On the wall there is a pale patch of clean wallpaper and an empty nail where a '
            'picture once hung.'),
    cam('npc_wynn_haven', 0.6),
    narrate('The clothier, Wynn, sleeps in her chair beside the loom, her head on her arm. The snipping has stopped.'),
    flag('ACT1_CLOTHIER_SEEN'),
    *end_control(),
]})
wake_tries('ACT1_CLOTHIER', 'npc_wynn_haven', 'clothier_havenbrook',
           ['You shake her shoulder. Nothing.',
            'You say her name, close to her ear. Her fingers twitch, as if pinching a needle.',
            'You shake her again, harder. Nothing. Somewhere far off, too faint to be in this room, a pair of '
            'shears snips once.'],
           'ACT1_CLOTHIER_STILL_DREAMING', ['ACT1_CLOTHIER_SEEN'],
           [sfx('chime', 0.3, 0.7), narrate('The Dreamcatcher hums, and the threads on the wall quiver.')])
S.append({'id': 'act1_clothier_catch', 'on': {'talk': 'npc_wynn_haven', 'map': 'clothier_havenbrook'},
          'needs': 'vigil_dreamcatcher', 'when': when(['ACT1_CLOTHIER_STILL_DREAMING'], ['ECHO_CLOTHIER_SNIP']),
          'steps': [
    *threads("Threads of pale light drift from the clothier's chest into the hoop. The web spins and pulls you in."),
    flag('ACT1_CLOTHIER_DREAM_ENTERED'),
    fx('dream', map='dream_clothier', spawn='arrival', story=True),
    wait(1.4),
    bars(False),
]})
# 43B: the dream shop.
S.append({'id': 'act1_clothier_dream', 'on': {'enter': 'dream_clothier'},
          'when': when(['ACT1_CLOTHIER_DREAM_ENTERED'], ['ACT1_CLOTHIER_DREAM_INTRO']), 'steps': [
    bars(True, 0.0),
    music('dream', 2.0),
    cam('dc_cam', zoom=0.95),
    wait(0.8),
    narrate('You open your eyes in the dream shop. It is the same shop, warped: the ceiling too high, the shelves '
            'leaning, the light a moonless violet. The wall of thread is a black snarl, knotted beyond cutting.'),
    cam('dc_spool', 0.6),
    narrate('A giant spool of black thread hangs from the rafters, its thread running down to the clothier, bound in '
            'her chair.'),
    cam('dc_form', 0.6),
    narrate('The dress form still stands in the middle of the room, bare. The desk drawer is empty. There are no '
            'scissors here.'),
    cam('dc_portrait', 0.6),
    narrate('On the wall where the real shop shows only a nail, a portrait hangs: a woman in a gown, painted '
            'life-size, in three clear colours.'),
    flag('ACT1_CLOTHIER_DREAM_INTRO'),
    *end_control(),
]})
S.append({'id': 'act1_clothier_dream_again', 'on': {'enter': 'dream_clothier'},
          'when': when(['ACT1_CLOTHIER_DREAM_INTRO'], ['ACT1_CLOTHIER_DRESS_DONE']), 'steps': [music('dream', 2.0)]})
S.append({'id': 'act1_clothier_fight_again', 'on': {'enter': 'dream_clothier'},
          'when': when(['ACT1_CLOTHIER_FIGHT'], ['ACT1_CLOTHIER_BOSS_DOWN']), 'steps': [music('boss', 0.8)]})
S.append({'id': 'act1_clothier_portrait', 'on': {'use': 'clothier_portrait', 'map': 'dream_clothier'},
          'when': when(['ACT1_CLOTHIER_DREAM_INTRO'], ['ACT1_CLOTHIER_PORTRAIT_SEEN']), 'steps': [
    bars(True),
    cam('dc_portrait', 0.5),
    narrate('A woman in a gown, painted life-size: a crimson bodice, an ivory skirt, a gold sash.'),
    narrate('You sketch the dress into your journal.'),
    flag('ACT1_CLOTHIER_PORTRAIT_SEEN'),
    *end_control(),
]})
S.append({'id': 'act1_clothier_portrait_again', 'on': {'use': 'clothier_portrait', 'map': 'dream_clothier'},
          'when': when(['ACT1_CLOTHIER_PORTRAIT_SEEN'], ['ACT1_CLOTHIER_DRESS_DONE']),
          'steps': [narrate('The portrait: a crimson bodice, an ivory skirt, a gold sash.')]})
S.append({'id': 'act1_clothier_snarl', 'on': {'use': 'thread_snarl', 'map': 'dream_clothier'},
          'when': when([], ['ECHO_CLOTHIER_SNIP']),
          'steps': [narrate('The thread is knotted beyond cutting, and there is nothing here to cut it with.')]})

# 43C: the shears, and the thread wall, in Solace.
S.append({'id': 'act1_clothier_shears', 'on': {'use': 'clothier_shears_desk', 'map': 'clothier_havenbrook'},
          'when': when(['ACT1_CLOTHIER_SEEN'], ['ACT1_CLOTHIER_SHEARS']), 'steps': [
    narrate("Behind the desk lies a pair of long dressmaker's shears."),
    {'do': 'give', 'item': 'clothier_shears'},
    flag('ACT1_CLOTHIER_SHEARS'),
]})
for colour in [t[0] for t in THREADS] + SPARES:
    up = colour.upper()
    S.append({'id': 'act1_cut_' + colour, 'on': {'use': 'thread_' + colour, 'map': 'clothier_havenbrook'},
              'needs': 'clothier_shears', 'when': when([], ['ACT1_THREAD_CUT_' + up, 'ECHO_CLOTHIER_SNIP']), 'steps': [
        sfx('snip', 0.9, 1.0),
        narrate('You snip the %s thread from its hooks. The snip rings in the silent shop.' % colour),
        {'do': 'give', 'item': 'thread_' + colour},
        flag('ACT1_THREAD_CUT_' + up),
    ]})
    S.append({'id': 'act1_cut_%s_bare' % colour, 'on': {'use': 'thread_' + colour, 'map': 'clothier_havenbrook'},
              'when': when([], ['ACT1_THREAD_CUT_' + up, 'ECHO_CLOTHIER_SNIP']),
              'steps': [narrate('The threads are strung tight from hook to hook. You would need something sharp.')]})
# The three the portrait wears, in the bag: the threads are cut.
for colour, _, _ in THREADS:
    S.append({'id': 'act1_threads_cut_' + colour, 'on': {'flag': 'ACT1_THREAD_CUT_' + colour.upper()},
              'when': when(['ACT1_THREAD_CUT_' + c.upper() for c, _, _ in THREADS], ['ACT1_CLOTHIER_THREADS_CUT']),
              'steps': [flag('ACT1_CLOTHIER_THREADS_CUT')]})

# 43D: the dress, a piece at a time -- a right thread blooms, a wrong one is refused.
order = []
for k, (colour, piece_flag, piece) in enumerate(THREADS):
    before = ['ACT1_DRESS_' + THREADS[j][1] for j in range(k)]
    order.append({'id': 'act1_dress_' + piece, 'on': {'use': 'dress_form_dream', 'map': 'dream_clothier'},
                  'needs': 'thread_' + colour,
                  'when': when(['ACT1_CLOTHIER_DREAM_INTRO'] + before, ['ACT1_DRESS_' + piece_flag]), 'steps': [
        {'do': 'take', 'item': 'thread_' + colour},
        fx('flash', colour=[255, 236, 220], amount=0.35),
        narrate({'bodice': 'You lay the crimson thread against the dress form. Cloth blooms from it and stitches '
                           'itself into a bodice.',
                 'skirt': 'The ivory thread spills down from the waist and widens into a full skirt.',
                 'sash': 'The gold thread wraps the waist and ties itself into a sash. The dress is exactly the '
                         'dress in the portrait.'}[piece]),
        flag('ACT1_DRESS_' + piece_flag),
    ]})
S.extend(order)
# A thread of the dress, but out of its turn.
for colour, piece_flag, piece in THREADS:
    S.append({'id': 'act1_dress_%s_early' % piece, 'on': {'use': 'dress_form_dream', 'map': 'dream_clothier'},
              'needs': 'thread_' + colour, 'when': when(['ACT1_CLOTHIER_DREAM_INTRO'], ['ACT1_DRESS_' + piece_flag]),
              'steps': [narrate('The %s thread slides off the bare form. Not yet: the dress is made from the bodice '
                                'down.' % colour)]})
for colour in SPARES:
    S.append({'id': 'act1_dress_refuse_' + colour, 'on': {'use': 'dress_form_dream', 'map': 'dream_clothier'},
              'needs': 'thread_' + colour, 'when': when(['ACT1_CLOTHIER_DREAM_INTRO'], ['ACT1_CLOTHIER_DRESS_DONE']),
              'steps': [narrate('The %s thread will not take. The form shrugs it off, and it is back in your hand. '
                                'Nothing in the portrait is that colour.' % colour)]})
S.append({'id': 'act1_dress_nothing', 'on': {'use': 'dress_form_dream', 'map': 'dream_clothier'},
          'when': when(['ACT1_CLOTHIER_DREAM_INTRO'], ['ACT1_CLOTHIER_DRESS_DONE']),
          'steps': [narrate('Nothing to dress it with.')]})
# The last piece on: the room quakes, and the form comes alive.
S.append({'id': 'act1_dress_done', 'on': {'flag': 'ACT1_DRESS_SASH'},
          'when': when([], ['ACT1_CLOTHIER_DRESS_DONE']), 'steps': [
    bars(True),
    cam('dc_form', 0.5),
    music('', 0.8),
    narrate('The shop goes silent. Then the floor shudders.'),
    sfx('impact', 0.8, 0.6),
    fx('shatter', at='dc_portrait', radius=40, amount=0.35),
    sfx('shatter', 0.8, 1.0),
    narrate('Shelves rattle, spools tumble from the rafters, and the portrait slides from its nail and shatters.'),
    flag('ACT1_CLOTHIER_DRESS_DONE'),
    wait(0.6),
    sfx('grind', 0.8, 1.3),
    narrate("The dress form's head jerks upright. Its wooden hands split lengthwise and stretch into long, gleaming "
            'blades, a scissor blade where each finger should be. The finished dress hangs from it, snagged with '
            'pins.'),
    narrate('Bound in her chair, the clothier stirs and goes still. The mannequin steps off its stand with a screech '
            'of metal.'),
    flag('ACT1_CLOTHIER_FIGHT'),
    music('boss', 0.4),
    *end_control(),
]})
# 43E: the Mannequin falls; the thread can be cut.
S.append({'id': 'act1_mannequin_down', 'on': {'flag': 'ACT1_CLOTHIER_BOSS_DOWN'},
          'when': when([], ['ACT1_MANNEQUIN_SAID']), 'steps': [
    flag('ACT1_MANNEQUIN_SAID'),
    music('dream', 2.0),
    narrate('She falls. The blades fold back into wooden fingers, and the finished dress slumps empty on the floor. '
            'Above, the giant spool of black thread trembles.'),
]})
S.append({'id': 'act1_clothier_anchor_held', 'on': {'use': 'clothier_anchor', 'map': 'dream_clothier'},
          'when': when([], ['ACT1_CLOTHIER_BOSS_DOWN']),
          'steps': [narrate('The black thread is drawn tight as wire. Nothing will part it while the dress form stands.')]})
S.append({'id': 'act1_clothier_anchor_bare', 'on': {'use': 'clothier_anchor', 'map': 'dream_clothier'},
          'when': when(['ACT1_CLOTHIER_BOSS_DOWN'], ['ECHO_CLOTHIER_SNIP']),
          'steps': [narrate('The thread is too thick to break by hand. There are shears in the waking shop.')]})
S.insert(len(S) - 1, {'id': 'act1_clothier_anchor', 'on': {'use': 'clothier_anchor', 'map': 'dream_clothier'},
          'needs': 'clothier_shears', 'when': when(['ACT1_CLOTHIER_BOSS_DOWN'], ['ECHO_CLOTHIER_SNIP']), 'steps': [
    bars(True),
    cam('dc_spool', 0.5),
    narrate("You open the waking shop's shears around the black thread, and close them."),
    sfx('snip', 1.0, 0.85),
    flag('ECHO_CLOTHIER_SNIP'),
    narrate('The snip echoes through the dream. The thread parts and the Reverie dissolves.'),
    white_out(0.5),
    music('', 0.6),
    # 43F: the shop in Solace.
    wake('clothier_havenbrook', 'wynn_wake'),
    face('player', 'npc_wynn_haven'),
    cam('npc_wynn_haven'),
    wait(0.5),
    fade('clear', 1.4),
    narrate('Afternoon light fills the shop, and a single loud snip rings through the room.'),
    sfx('snip', 1.0, 0.9),
    pose('npc_wynn_haven', ''),
    say('npc_wynn_haven', '(gasping, then slowly focusing) The dress... it wouldn\'t finish. And then the shears.'),
    face('npc_wynn_haven', 'player'),
    {'do': 'take', 'item': 'clothier_shears'},
    *[{'do': 'take', 'item': 'thread_' + c} for c in SPARES],
    narrate('She looks at you. The shears lie on the desk again, as if they had never left.'),
    say('npc_wynn_haven', 'You came into my dream and cut me out of it. I felt the thread go.'),
    narrate('She steadies herself against the loom.'),
    say('npc_wynn_haven', 'Take the loom. It has sat idle too long. Make yourself something worth wearing.'),
    flag('ACT1_HAVENBROOK_LOOM_OPEN'),
    flag('ACT1_CLOTHIER_AWAKE'),
    *end_control(),
]})

# ---------------------------------------------------------------------------------------------
#  Scenes 44-50 -- the Mayor's Dream
# ---------------------------------------------------------------------------------------------
# All four saved -- Halda, Bess, the Tanner and the clothier, in any order --
# and the finale opens (its door asks first: genmaps, Portal "ask"). On
# whichever wakes last; and on going into the town, for a game saved with the
# three of before and the clothier saved since.
FOUR = ['ACT1_HALDA_AWAKE', 'ACT1_BESS_AWAKE', 'ACT1_TANNER_AWAKE', 'ACT1_CLOTHIER_AWAKE']
for last in FOUR:
    S.append({'id': 'act1_finale_open_' + last.split('_')[1].lower(), 'on': {'flag': last},
              'when': when(FOUR, ['ACT1_FINALE_QUEST_START']), 'steps': [
        flag('ACT1_FINALE_QUEST_START'),
        quest('q_act1_finale'),
    ]})
S.append({'id': 'act1_finale_enter', 'on': {'enter': 'mayor_hall'},
          'when': when(['ACT1_FINALE_READY'], ['ACT1_FINALE_TRAPPED']), 'steps': [
    bars(True, 0.0),
    cam('mh_cam', zoom=1.05),
    wait(0.6),
    narrate("The room is as you left it, papers still scattered across the floor. But Mayor Hale's chair is empty, "
            "and his sleeping body is gone."),
    walk('player', 'mh_desk_front', 52),
    face('player', d='up'),
    narrate('On the desk lies a single folded note.'),
    sfx('laugh', 0.9),
    wait(1.6),
    narrate('You freeze, then unfold the note.'),
    {'do': 'note', 'title': '', 'text': 'Boo.'},
    music('trap', 0.1),
    flag('ACT1_FINALE_TRAPPED'),
    fx('tear', map='dream_mayor_hall', spawn='arrival'),
    wait(1.6),
    bars(False),
]})
# Thrown out of it -- a fall in the dream wakes you at the Mayor's desk -- and
# back in by the same note, from where you had got to.
for sid, when_flags, when_not, to_map, to_spawn in [
        ('act1_finale_retry_hall', ['ACT1_FINALE_TRAPPED'], ['ACT1_MAYOR_HALL_CLEARED'], 'dream_mayor_hall', 'arrival'),
        ('act1_finale_retry_town', ['ACT1_MAYOR_HALL_CLEARED'], ['ECHO_HAVENBROOK_FINAL_BELL'],
         'prologue_dream_havenbrook', 'from_dream_mayor')]:
    S.append({'id': sid, 'on': {'use': 'mh_note', 'map': 'mayor_hall'}, 'when': when(when_flags, when_not + ['ECHO_HAVENBROOK_FINAL_BELL']),
              'steps': [
        {'do': 'confirm', 'text': "The note lies where you dropped it. Once you're in, you can't leave until it's "
                                  'done. Ready?', 'yes': 'Read it again', 'no': 'Not yet'},
        sfx('laugh', 0.7),
        music('trap', 0.1),
        fx('tear', map=to_map, spawn=to_spawn),
        wait(1.6),
    ]})
S.append({'id': 'act1_mayor_dream', 'on': {'enter': 'dream_mayor_hall'},
          'when': when(['ACT1_FINALE_TRAPPED'], ['ACT1_MAYOR_HALL_CLEARED']), 'steps': [
    bars(True, 0.0),
    music('trap', 0.3),
    cam('dmh_cam'),
    wait(0.5),
    narrate('The hall as the Reverie bends it: papers drift in the air, the walls lean inward, and the desk tilts as '
            'if on a slope. The doors are shut. The windows are black.'),
    flag('ACT1_MH_GO'),
    narrate('Black Knights form out of the shadows around the room, one by one. Swords scrape from their scabbards.'),
    *end_control(),
]})
S.append({'id': 'act1_mayor_cleared', 'on': {'flag': 'ACT1_MAYOR_HALL_CLEARED'},
          'when': when([], ['ACT1_MH_DOORS']), 'steps': [
    flag('ACT1_MH_DOORS'),
    sfx('door', 1.0, 0.6),
    narrate('As the last knight falls, the black armour collapses into cold scrap. The doors groan open.'),
]})
S.append({'id': 'act1_streets', 'on': {'enter': 'prologue_dream_havenbrook', 'spawn': 'from_dream_mayor'},
          'when': when(['ACT1_MAYOR_HALL_CLEARED'], ['ACT1_VASK_SHOUT']), 'steps': [
    bars(True, 0.0),
    music('dream_town', 1.0),
    cam('player'),
    wait(0.4),
    narrate('You burst out of the Mayor\'s Hall into a Havenbrook that is still the Reverie: a bruised violet sky, '
            'leaning houses, silent streets. The Hushed are gone, but the dream remains.'),
    {'do': 'camera', 'follow': 'player'},
    walk('player', 'dream_vask_front', 96, clip='run'),
    face('player', d='up'),
    cam('dream_vask_cam', 0.5),
    narrate('On the porch, Elder Vask sits rocking, eyes wide open. He does not stop rocking. He jabs his cane at the '
            "Guild Hall's doors."),
    say('npc_vask_dream', '(shouting) INSIDE. NOW.'),
    flag('ACT1_VASK_SHOUT'),
    *end_control(),
]})
S.append({'id': 'act1_streets_again', 'on': {'enter': 'prologue_dream_havenbrook'},
          'when': when(['ACT1_VASK_SHOUT'], ['ECHO_HAVENBROOK_FINAL_BELL']), 'steps': [music('dream_town', 1.0)]})
S.append({'id': 'act1_guild_dream', 'on': {'enter': 'dream_guild_hall'},
          'when': when(['ACT1_MAYOR_HALL_CLEARED'], ['ACT1_FINALE_GUILD_ENTERED']), 'steps': [
    bars(True, 0.0),
    music('', 1.0),
    cam('dgh_cam', zoom=0.95),
    wait(0.6),
    narrate('The Guild Hall is dark and ominous. Long velvet runner carpets stretch down the length of the room. Along '
            'both runners, Black Knights stand in two straight lines, facing inward toward the middle of the room, '
            'motionless.'),
    *end_control(),
]})
S.append({'id': 'act1_vexel_glimpse', 'on': {'near': 'dgh_desk_near', 'map': 'dream_guild_hall', 'radius': 100},
          'when': when(['ACT1_MAYOR_HALL_CLEARED'], ['ACT1_FINALE_GUILD_ENTERED']), 'steps': [
    bars(True),
    cam('dgh_anchor_cam', 1.0),
    narrate('Behind the guild\'s great desk towers a huge Anchor: a mountain of black thread wound around something '
            'that glows faintly through it, the last Dawn Bell of Havenbrook. Bound in the thread on either side hang '
            'Mayor Hale and Guild Master Orlend, heads bowed, unconscious.'),
    {'do': 'spawn', 'actor': 'vexel', 'at': 'dgh_vexel', 'face': 'up'},
    wait(1.0),
    narrate('Beside the Anchor stands a tall figure with his back to you.'),
    walk('player', 'dgh_vexel_close', 44),
    fx('vanish', who='vexel'),
    wait(1.2),
    {'do': 'remove', 'actor': 'vexel'},
    sfx('grind', 1.0, 0.8),
    narrate("The knights' visors flare violet. The two lines break as they turn on you."),
    flag('ACT1_FINALE_GUILD_ENTERED'),
    music('boss', 0.4),
    *end_control(),
]})
S.append({'id': 'act1_guild_dream_again', 'on': {'enter': 'dream_guild_hall'},
          'when': when(['ACT1_FINALE_GUILD_ENTERED'], ['ACT1_FINALE_ANCHOR_DOWN']), 'steps': [music('boss', 0.8)]})
S.append({'id': 'act1_anchor_wakes', 'on': {'flag': 'ACT1_GUILD_KNIGHTS_DOWN'},
          'when': when([], ['ACT1_ANCHOR_WAKES']), 'steps': [
    bars(True),
    cam('dgh_anchor_cam', 0.6),
    flag('ACT1_ANCHOR_WAKES'),
    sfx('roar', 0.8, 0.5),
    narrate('When the last knight falls, the Anchor shudders awake, and its threads lash across the hall.'),
    *end_control(),
]})
S.append({'id': 'act1_final_bell_reveal', 'on': {'flag': 'ACT1_FINALE_ANCHOR_DOWN'},
          'when': when([], ['ACT1_BELL_REVEALED']), 'steps': [
    bars(True),
    music('', 1.5),
    cam('dgh_anchor_cam', 0.6),
    narrate('The Anchor unravels in a rush of black thread, and Mayor Hale and Guild Master Orlend sag free, '
            'unconscious but safe.'),
    flag('ACT1_BELL_REVEALED'),
    narrate('Where the Anchor stood, the last Dawn Bell is revealed: a great, pale bell in an old frame behind the '
            'desk, glowing softly.'),
    *end_control(),
]})
S.append({'id': 'act1_final_bell', 'on': {'use': 'guild_dawn_bell', 'map': 'dream_guild_hall'},
          'when': when(['ACT1_BELL_REVEALED'], ['ECHO_HAVENBROOK_FINAL_BELL']), 'steps': [
    bars(True),
    cam('dgh_anchor_cam', 0.4),
    sfx('bell', 1.0, 0.85),
    narrate('The bell tolls, deep and loud. The sound rolls out of the hall and echoes outside, all through the town.'),
    fx('golden', at='dgh_anchor_cam'),
    sfx('bell', 0.6, 0.85),
    narrate('The dream shudders and begins to come apart.'),
    fx('shatter', at='dgh_anchor_cam', radius=160, amount=0.4),
    flag('ECHO_HAVENBROOK_FINAL_BELL'),
    white_out(0.6),
    music('', 0.6),
    wake('guild_hall', 'gh_player_stand'),
    face('player', d='up'),
    cam('gh_cam', zoom=1.05),
    wait(0.6),
    fade('clear', 1.6),
    narrate('The Guild Hall in Solace is bright, sunlight pouring through its tall windows. The Black Knights lie in '
            'heaps of cold scrap along the carpets. Mayor Hale and Guild Master Orlend stir on the floor by the desk, '
            'blinking.'),
    pose('npc_mayor_guild', ''),
    face('npc_mayor_guild', 'player'),
    say('npc_mayor_guild', "(hoarse) Is it... morning? I dreamed I was sitting at my desk, and the whole town was "
                           "waiting on an answer I couldn't give."),
    narrate('He sees you, and his eyes fill.'),
    say('npc_mayor_guild', "You went down and came back up. Then you did it again, for every one of us. Havenbrook "
                           "owes you more than I can say. There's a house here in town with a good bed in it. It's "
                           "yours, free, for as long as you want it."),
    pose('npc_guildmaster', ''),
    face('npc_guildmaster', 'player'),
    say('npc_guildmaster', "(pushing up from the floor, gruff) The Guild has never owed anyone what it owes you. Take "
                           "your pick: an amulet, a ring, or a hood, each made for the Guild's finest. We insist."),
    narrate('The Mayor presses a house key into your hand. The Guild Master sets an amulet, a ring and a hood on the '
            'desk.'),
    flag('ACT1_FINALE_THANKED'),
    *end_control(),
]})
# ---------------------------------------------------------------------------------------------
#  Scenes 50-62 -- Word to the Neighbors
# ---------------------------------------------------------------------------------------------
# Scene 50: the gift chosen, the Mayor looks out at the waking town. A game
# saved between the gift and this scene hears it on coming back into the hall.
NEIGHBORS_50 = [
    bars(True),
    cam('gh_cam', zoom=1.05),
    narrate('Mayor Hale steadies himself against the desk and looks out the tall windows at the waking town. The '
            'warmth drains from his face.'),
    say('npc_mayor_guild', "(quietly) We woke because you stood between us and the dark. But we aren't the only "
                           "village on this road. It runs down through Whisperwood and splits: one branch to "
                           "Mossvale, the other to Fernhollow. If that man could reach into our beds, he can reach "
                           "into theirs."),
    say('npc_guildmaster', "(gruff) Warn them. Tell them about the sleeping, the knights, the black thread. And keep "
                           "your eyes open in the trees. Whisperwood was never a friendly road, and I doubt it's "
                           "friendlier now."),
    say('npc_mayor_guild', 'Go, and come back to us. Whatever you find, we need to hear it.'),
    flag('ACT1_NEIGHBORS_QUEST_START'),
    quest('q_act1_neighbors'),
    *end_control(),
]
S.append({'id': 'act1_neighbors', 'on': {'flag': 'ACT1_FINALE_GIFT_CHOSEN', 'map': 'guild_hall'},
          'when': when(['PROLOGUE'], ['ACT1_NEIGHBORS_QUEST_START', 'ACT1_DRAGON_SHADOW_SEEN']),
          'steps': NEIGHBORS_50})
S.append({'id': 'act1_neighbors_owed', 'on': {'enter': 'guild_hall'},
          'when': when(['PROLOGUE', 'ACT1_FINALE_GIFT_CHOSEN'],
                       ['ACT1_NEIGHBORS_QUEST_START', 'ACT1_DRAGON_SHADOW_SEEN']),
          'steps': [bars(True, 0.0), wait(0.4), *NEIGHBORS_50]})

# Scene 51: the Whisperwood, and Vexel three times in the trees. Each glimpse
# comes as the road reaches its zone and goes when he is looked at straight --
# or stepped toward, or passed -- leaving a curl of smoke.
S.append({'id': 'act1_ww_enter', 'on': {'enter': 'whisperwood_trail'},
          'when': when(['ACT1_NEIGHBORS_QUEST_START'], ['ACT1_WW_ENTERED', 'ACT1_FORK_REACHED']), 'steps': [
    flag('ACT1_WW_ENTERED'),
    narrate('The road narrows into Whisperwood: old trunks furred with moss, lichen hanging in curtains, light falling '
            'in soft, dusty shafts. The birdsong thins, then stops.'),
]})
GLIMPSE_FACE = {1: 'left', 2: 'up', 3: 'left'}
for k in (1, 2, 3):
    before = ['ACT1_WW_GONE_%d' % j for j in range(1, k)]
    shown, gone = 'ACT1_WW_GLIMPSE_%d' % k, 'ACT1_WW_GONE_%d' % k
    S.append({'id': 'act1_ww_glimpse_%d' % k, 'on': {'near': 'ww_zone_%d' % k, 'map': 'whisperwood_trail', 'radius': 150},
              'when': when(['ACT1_NEIGHBORS_QUEST_START'] + before, [shown, 'ACT1_FORK_REACHED']), 'steps': [
        {'do': 'spawn', 'actor': 'vexel', 'at': 'ww_vexel_%d' % k, 'face': GLIMPSE_FACE[k], 'keep': True},
        flag(shown),
    ]})
    # He is the glimpse's, kept; a game loaded since has none. So each way he goes first stands
    # him back where he was -- in the same frame, so nothing shows -- and then he goes.
    back = [{'do': 'remove', 'actor': 'vexel'},
            {'do': 'spawn', 'actor': 'vexel', 'at': 'ww_vexel_%d' % k, 'face': GLIMPSE_FACE[k]}]
    vanish = [fx('vanish', who='vexel', sink=False), {'do': 'remove', 'actor': 'vexel'}, flag(gone),
              count('ACT1_WHISPERWOOD_GLIMPSES')]
    gone_steps = back + vanish
    if k == 3:
        # "As you take a step, he lifts one hand in a slow, theatrical wave, and is gone."
        S.append({'id': 'act1_ww_wave', 'on': {'near': 'ww_vexel_3', 'map': 'whisperwood_trail', 'radius': 210},
                  'when': when([shown], [gone]), 'steps': [
            *back,
            face('vexel', 'player'),
            pose('vexel', 'attack'),
            wait(0.9),
            *vanish,
        ]})
    else:
        S.append({'id': 'act1_ww_look_%d' % k, 'on': {'look': 'ww_vexel_%d' % k, 'map': 'whisperwood_trail',
                                                         'radius': 260},
                  'when': when([shown], [gone]), 'steps': gone_steps})
        S.append({'id': 'act1_ww_step_%d' % k, 'on': {'near': 'ww_vexel_%d' % k, 'map': 'whisperwood_trail',
                                                         'radius': 170},
                  'when': when([shown], [gone]), 'steps': gone_steps})
    S.append({'id': 'act1_ww_pass_%d' % k, 'on': {'near': 'ww_past_%d' % k, 'map': 'whisperwood_trail', 'radius': 150},
              'when': when([shown], [gone]), 'steps': gone_steps})
S.append({'id': 'act1_fork', 'on': {'near': 'ww_fork', 'map': 'whisperwood_trail', 'radius': 130},
          'when': when(['ACT1_NEIGHBORS_QUEST_START'], ['ACT1_FORK_REACHED']), 'steps': [
    {'do': 'remove', 'actor': 'vexel'},
    bars(True),
    cam('ww_fork', 0.8),
    narrate('The trees open. At a weathered signpost the road forks. One arm points to Mossvale, the other to '
            'Fernhollow.'),
    flag('ACT1_FORK_REACHED'),
    *end_control(),
]})

# Scenes 52-55: Mossvale. Into it from the road, once, with the word to give:
# everyone asleep; Wynn's house empty; Oona by her pot and Apocolo's torn book;
# the inn, and Vexel's red eye.
S.append({'id': 'act1_mossvale', 'on': {'enter': 'mossvale'},
          'when': when(['ACT1_NEIGHBORS_QUEST_START'], ['ACT1_MOSSVALE_VISITED']), 'steps': [
    bars(True, 0.0),
    cam('player'),
    wait(0.5),
    narrate('You walk in under a low arch of living branches. Mossvale is a village of moss-roofed cottages and '
            'winding footpaths, and it is silent. A cart driver slumps on his seat, the horse standing patient and '
            'still. A woman sits against a fence, a basket of apples spilled around her. Wind chimes hang motionless '
            'in the doorways.'),
    narrate('You search the lanes. Everyone you pass is fast asleep.'),
    fade('black', 1.0),
    {'do': 'map', 'map': 'mossvale_weavers', 'spawn': 'entrance'},
    cam('player'),
    fade('clear', 1.0),
    narrate('Bolts of dyed cloth line the walls, and spools of thread glitter in the window light. The loom stands '
            'untouched, a half-woven bolt hanging from the frame, the pattern stopped mid-row. The shop is empty, and '
            'the door is unlocked.'),
    narrate('You search the shop and the rooms behind it. Nobody is home.'),
    fade('black', 1.0),
    {'do': 'map', 'map': 'mossvale_herbalist', 'spawn': 'entrance'},
    cam('npc_oona'),
    fade('clear', 1.0),
    narrate('Shelves of corked bottles, bundles of herbs drying from the rafters. A big iron pot bubbles over the '
            'fire, a glowing concoction rolling in slow, thick swells. Oona the brewer sleeps on a stool beside it, a '
            'long ladle still gripped in her hand, her chin on her chest.'),
    narrate('You look from Oona to the pot. It keeps boiling, and nobody is there to stir it.'),
    cam('apocolo_book_at', 0.6),
    narrate('On the workbench beside the pot lies a thick, leather-bound recipe book, splayed open. You turn the '
            'pages. The flyleaf reads APOCOLO, APOTHECARY OF HAVENBROOK, and beneath it, in a younger hand: "Taken '
            'by Vexel. This is all I have left of him."'),
    narrate('The one recipe still whole is written in his script, a cure for the sleepers, and a margin note warns '
            'that they carry a second sickness, a bayou hex laid by the voodoo priests. The steps that matter are '
            'gone. Four pages have been ripped out, and their torn stubs stand along the spine like broken teeth.'),
    narrate('Each stub carries a trace of where its page went: a greasy grey handprint, a green scale caught in the '
            'binding, a smear of grave dirt, and a tuft of coarse brown fur.'),
    narrate('You look down at Oona, sleeping beside the one thing that might wake her.'),
    flag('ACT1_OONA_BOOK_FOUND'),
    fade('black', 1.0),
    {'do': 'map', 'map': 'mossvale_lodge_hall', 'spawn': 'entrance'},
    cam('ml_vexel'),
    fade('clear', 1.0),
    narrate("The inn's common room. Patrons lie slumped over tables, mugs tipped, the innkeeper folded over the "
            'bar. The fire has burned down to embers.'),
    {'do': 'spawn', 'actor': 'vexel', 'at': 'ml_vexel', 'face': 'down'},
    wait(0.6),
    narrate('One figure stands in the middle of the room, utterly still among the sleepers: Vexel Von Finch. He '
            'turns his skull-like face toward you and grimaces, as if the sight of you were a bad taste.'),
    face('vexel', 'player'),
    narrate('One eye kindles red under the hood.'),
    fx('beam', **{'from': 'vexel', 'lift': 46, 'spread': 34, 'time': 1.8}),
    sfx('chime', 0.3, 0.4),
    wait(1.9),
    fx('flash', colour=[196, 150, 255], amount=0.55),
    fx('vanish', who='vexel'),
    wait(0.8),
    {'do': 'remove', 'actor': 'vexel'},
    narrate('Then a violet flash, and he is gone.'),
    flag('ACT1_MOSSVALE_VISITED'),
    quest('q_act2_torn_pages'),
    *end_control(),
]})

# Scenes 56-58: Fernhollow. In through the gate, once: not a soul awake;
# Wendel asleep by his window; the lone house by the south-east wall, and the
# door that falls in.
S.append({'id': 'act1_fernhollow', 'on': {'enter': 'fernhollow'},
          'when': when(['ACT1_NEIGHBORS_QUEST_START'], ['ACT1_FERNHOLLOW_DOOR_BROKEN']), 'steps': [
    bars(True, 0.0),
    cam('player'),
    wait(0.5),
    narrate('You step through the gate of walled Fernhollow. Lanterns still burn in broad daylight. Laundry hangs '
            'motionless on its lines, and a pond glints at the village\'s edge.'),
    narrate('You turn slowly, scanning doorways, windows and rooftops for anyone awake. Not one soul moves.'),
    fade('black', 1.0),
    {'do': 'map', 'map': 'fernhollow_cottage', 'spawn': 'entrance'},
    cam('npc_wendel_home'),
    fade('clear', 1.0),
    narrate("A small house sits beside the pond, nets drying on the porch rail. Inside, an old man, Wendel, "
            "Fernhollow's angler, is asleep in a chair by the window, a fishing line still wound around his fingers."),
    narrate('You shake his shoulder, and again. The old man does not stir.'),
    fade('black', 1.0),
    {'do': 'map', 'map': 'fernhollow', 'spawn': 'mara_lane'},
    cam('mara_door'),
    fade('clear', 1.0),
    narrate("A narrow lane runs along the town wall in Fernhollow's south-east corner. A lone house leans against "
            'the stone, shutters closed, a bucket knocked on its side by the step.'),
    walk('player', 'mara_step', 50),
    face('player', d='up'),
    sfx('door', 0.5, 1.4),
    wait(0.4),
    sfx('door', 0.5, 1.4),
    wait(0.4),
    sfx('door', 0.5, 1.4),
    wait(0.7),
    sfx('impact', 1.0, 0.7),
    flag('ACT1_FERNHOLLOW_DOOR_BROKEN'),
    narrate('Knock. Knock. Knock. The door falls inward off its hinges and crashes flat on the floor.'),
    fade('black', 0.8),
    {'do': 'map', 'map': 'fernhollow_mara', 'spawn': 'entrance'},
    cam('player'),
    fade('clear', 1.0),
    narrate("A woman's coat still hangs by the door, and a half-eaten supper sits cold on the table. Furniture is "
            'overturned, a chair lies in splinters, crockery is smashed across the floor, and long, ragged scratches '
            'score the walls. A struggle happened here.'),
    narrate('You search every room. Nobody is home.'),
    *end_control(),
]})
# What the house shows, looked at.
for obj, line in [('mara_scratches', 'Long, ragged scratches score the plaster, four at a time, too far apart for a '
                                     'hand.'),
                  ('mara_chair', 'A chair, in splinters. Somebody swung it, or was thrown into it.'),
                  ('mara_lantern', 'A lantern on its side, the glass cracked, the wick burned out.'),
                  ('mara_supper', 'A half-eaten supper, gone cold. Whoever sat down to it did not finish it.')]:
    S.append({'id': 'act1_' + obj, 'on': {'use': obj, 'map': 'fernhollow_mara'}, 'steps': [narrate(line)]})
# Her bed: "Sleep", like the cell mattress.
S.append({'id': 'act1_mara_sleep', 'on': {'use': 'mara_bed', 'map': 'fernhollow_mara'},
          'when': when(['ACT1_FERNHOLLOW_DOOR_BROKEN'], ['ACT1_FERNHOLLOW_VISITED']), 'steps': [
    bars(True),
    narrate('You lie down on the villager\'s bed. It is cold, as if nobody has slept in it for days.'),
    fx('dream', map='dream_mara', spawn='arrival', story=True),
    wait(1.4),
    bars(False),
]})
S.append({'id': 'act1_mara_slept', 'on': {'use': 'mara_bed', 'map': 'fernhollow_mara'},
          'when': when(['ACT1_FERNHOLLOW_VISITED'], []), 'steps': [
    narrate('Her bed. You have seen what happened here.')]})

# Scene 59: the dream-lit house, and the echo of the struggle a room at a time.
S.append({'id': 'act1_mara_dream', 'on': {'enter': 'dream_mara'},
          'when': when(['ACT1_FERNHOLLOW_DOOR_BROKEN'], ['ACT1_MARA_DREAM_IN']), 'steps': [
    bars(True, 0.0),
    music('dream', 2.0),
    cam('player'),
    wait(0.6),
    narrate('You rise in the dream-lit house. Somewhere ahead, a crash. Shouting. A low, layered hum.'),
    flag('ACT1_MARA_DREAM_IN'),
    *end_control(),
]})
ECHO = {'alpha': 0.42, 'flicker': True, 'keep': True}
S.append({'id': 'act1_mara_echo_1', 'on': {'near': 'dm_room_1', 'map': 'dream_mara', 'radius': 80},
          'when': when(['ACT1_MARA_DREAM_IN'], ['ACT1_MARA_ECHO_1']), 'steps': [
    bars(True),
    cam('dm_room_1', 0.5),
    sfx('impact', 0.5, 1.2),
    dict({'do': 'spawn', 'actor': 'mara_echo', 'at': 'dm_room_1', 'dx': 18, 'face': 'left'}, **ECHO),
    *[dict({'do': 'spawn', 'actor': 'echo_hushed%d' % (j + 1), 'at': 'dm_room_1', 'dx': dx, 'dy': dy, 'face': f},
           **ECHO) for j, (dx, dy, f) in enumerate([(-50, -10, 'right'), (-30, 34, 'right'), (40, -34, 'left')])],
    narrate('Stepping through the doorway, the room flickers into a faded, translucent echo: a woman with a fire '
            'poker in her fist backs across the floor as a ring of Hushed closes in.'),
    pose('mara_echo', 'shake'),
    sfx('swing', 0.7, 0.9),
    narrate('A chair flies. She fights with everything she has, driving them back, barely staying on her feet.'),
    flag('ACT1_MARA_ECHO_1'),
    *[{'do': 'remove', 'actor': a} for a in ('mara_echo', 'echo_hushed1', 'echo_hushed2', 'echo_hushed3')],
    *end_control(),
]})
S.append({'id': 'act1_mara_echo_2', 'on': {'near': 'dm_room_2', 'map': 'dream_mara', 'radius': 80},
          'when': when(['ACT1_MARA_ECHO_1'], ['ACT1_MARA_ECHO_2']), 'steps': [
    bars(True),
    cam('dm_room_2', 0.5),
    dict({'do': 'spawn', 'actor': 'mara_echo', 'at': 'dm_room_2', 'face': 'down'}, **ECHO),
    *[dict({'do': 'spawn', 'actor': 'echo_hushed%d' % (j + 1), 'at': 'dm_room_2', 'dx': dx, 'dy': dy, 'face': f},
           **ECHO) for j, (dx, dy, f) in enumerate([(-44, 0, 'right'), (44, 0, 'left'), (0, -40, 'down'),
                                                    (0, 40, 'up')])],
    narrate('In the next room the hum swells. The Hushed press in from every side, and she is losing ground.'),
    pose('mara_echo', 'shake'),
    flag('ACT1_MARA_ECHO_2'),
    *[{'do': 'remove', 'actor': a} for a in ('mara_echo', 'echo_hushed1', 'echo_hushed2', 'echo_hushed3',
                                             'echo_hushed4')],
    *end_control(),
]})
S.append({'id': 'act1_mara_echo_3', 'on': {'near': 'dm_room_3', 'map': 'dream_mara', 'radius': 80},
          'when': when(['ACT1_MARA_ECHO_2'], ['ACT1_FERNHOLLOW_VISITED']), 'steps': [
    bars(True),
    cam('dm_room_3', 0.5),
    dict({'do': 'spawn', 'actor': 'mara_echo', 'at': 'dm_room_3', 'dx': -10, 'face': 'up'}, **ECHO),
    {'do': 'spawn', 'actor': 'rival', 'at': 'dm_window', 'face': 'down', 'keep': True},
    {'do': 'tint', 'who': 'rival', 'colour': [86, 74, 104]},
    face('rival', d='right'),
    dict({'do': 'spawn', 'actor': 'echo_knight1', 'at': 'dm_window', 'dx': -34, 'dy': 22, 'face': 'down'}, **ECHO),
    dict({'do': 'spawn', 'actor': 'echo_knight2', 'at': 'dm_window', 'dx': 34, 'dy': 22, 'face': 'down'}, **ECHO),
    {'do': 'tint', 'who': 'echo_knight1', 'colour': [74, 68, 96]},
    {'do': 'tint', 'who': 'echo_knight2', 'colour': [74, 68, 96]},
    sfx('impact', 0.7, 0.9),
    narrate('In the last room, Black Knights vault in through the shutters. They seize her arms and wrench the poker '
            'away.'),
    walk('mara_echo', 'dm_throw', 120, wait=True),
    sfx('impact', 0.9, 0.7),
    flag('ACT1_MARA_SCUFFS'),
    narrate('One heaves her across the room, and she hits the far wall and slides down it.'),
    narrate('Behind the knights, in the cracked shutters of the window, stands one figure that does not flicker. It '
            'is solid and in full colour while everything else is a faded echo: a hooded silhouette, face hidden, '
            'hands loose at its sides. It does not move to help the knights, and it does not stop them.'),
    face('rival', 'player'),
    flag('ACT1_RIVAL_SIGN_SEEN'),
    wait(0.9),
    narrate('It turns its head toward you, as if it knew it was being watched.'),
    narrate('As each blow lands, fresh scuffs and long scratch marks appear across the floor and walls, matching the '
            'damage in the waking house. The echo shudders, thins, and goes out.'),
    fx('vanish', who='echo_knight1', sink=False),
    fx('vanish', who='echo_knight2', sink=False),
    fx('vanish', who='mara_echo', sink=False),
    fx('vanish', who='rival', sink=False),
    wait(1.0),
    *[{'do': 'remove', 'actor': a} for a in ('mara_echo', 'echo_knight1', 'echo_knight2', 'rival')],
    narrate("The house is quiet. She isn't there."),
    flag('ACT1_FERNHOLLOW_VISITED'),
    *end_control(),
]})
# Out of it, by her bed: "Wake".
S.append({'id': 'act1_mara_wake', 'on': {'use': 'dm_bed', 'map': 'dream_mara'},
          'when': when(['ACT1_FERNHOLLOW_VISITED'], []), 'steps': [
    bars(True),
    white_out(0.5),
    music('', 0.6),
    wake('fernhollow_mara', 'mara_bedside'),
    cam('player'),
    wait(0.4),
    fade('clear', 1.2),
    *end_control(),
]})
S.append({'id': 'act1_mara_wake_early', 'on': {'use': 'dm_bed', 'map': 'dream_mara'},
          'when': when([], ['ACT1_FERNHOLLOW_VISITED']),
          'steps': [narrate('Not yet. Something is still happening in this house.')]})

# Where every sleeper of the neighbors and the college sleeps. A talk scene names
# the map it plays on, as every scene does (the self-test reads its steps there).
SLEEPS_ON = {
    'mossvale': ['npc_sela', 'npc_pell', 'npc_tamsin', 'npc_mv_carter', 'npc_mv_apple'],
    'mossvale_herbalist': ['npc_oona'],
    'mossvale_lodge_hall': ['npc_hadley', 'npc_mv_patron_1', 'npc_mv_patron_2'],
    'fernhollow': ['npc_maud', 'npc_pim', 'npc_college_porter', 'npc_mira', 'npc_nell', 'npc_ilse'],
    'fernhollow_cottage': ['npc_wendel_home', 'npc_hesper'],
    'fernhollow_college': ['npc_councillor_ferris', 'npc_councillor_wren'],
    'college_grounds': ['npc_college_walker_a', 'npc_college_walker_b', 'npc_college_reader', 'npc_college_gardener',
                        'npc_college_usher'],
    'college_training': ['npc_college_instructor'] + ['npc_college_lane_%d' % k for k in range(4)],
    'college_classroom': ['npc_college_lector'] + ['npc_college_pupil_%d' % k for k in range(5)],
}
HOME = {n: m for m, ns in SLEEPS_ON.items() for n in ns}

# Scene 64, as Act I first meets it: the Dreamcatcher on a sleeper in either
# town. Something is blocking the way in.
TOWN_SLEEPERS = ['npc_sela', 'npc_pell', 'npc_tamsin', 'npc_oona', 'npc_hadley', 'npc_mv_patron_1',
                 'npc_mv_patron_2', 'npc_mv_carter', 'npc_mv_apple',
                 'npc_maud', 'npc_pim', 'npc_college_porter', 'npc_mira', 'npc_wendel_home', 'npc_hesper', 'npc_nell',
                 'npc_ilse']
for npc in TOWN_SLEEPERS:
    S.append({'id': 'act1_blocked_' + npc[4:], 'on': {'talk': npc, 'map': HOME[npc]}, 'needs': 'vigil_dreamcatcher',
              'when': when(['PROLOGUE', 'ACT1_NEIGHBORS_QUEST_START'], ['CURED_' + npc, 'NEIGHBORS_SKIPPED']),
              'steps': [
        bars(True),
        {'do': 'spawn', 'actor': 'dreamcatcher_held', 'at': 'player', 'dy': -30, 'lift': 6},
        sfx('chime', 0.4, 0.75),
        narrate('You raise the Dreamcatcher over the sleeper. Its threads flare and pull toward them, and the world '
                'begins to tilt toward their dream.'),
        fx('flash', colour=[60, 40, 80], amount=0.5),
        sfx('bump', 0.9, 0.6),
        {'do': 'remove', 'actor': 'dreamcatcher_held'},
        narrate('Then something slams shut: a hard, silent shove, like a hand against the chest. The threads go slack, '
                'the glow gutters, and you stumble back, awake in the same spot.'),
        narrate('Something is blocking the way in.'),
        flag('ACT1_DREAMCATCHER_BLOCKED'),
        *end_control(),
    ]})

# Scene 60: the report, at the Guild Hall -- and (62) the dragon's shadow as
# the player steps out after it, and the Act II card.
DRAGON_62 = [
    cam('porch_cam', zoom=0.9),
    wait(0.6),
    narrate('Havenbrook is awake: townsfolk spill into the streets, smoke rises from the chimneys, and a hammer rings '
            'from the forge.'),
    sfx('anvil', 0.25, 1.1),
    wait(0.8),
    sfx('anvil', 0.25, 1.1),
    {'do': 'spawn', 'actor': 'wynn_door', 'at': 'clothier_door', 'dy': -20, 'face': 'up', 'keep': True},
    {'do': 'fade', 'to': 'black', 'time': 1.4, 'wait': False, 'amount': 0.32},
    wait(1.0),
    {'do': 'crowd', 'dir': 'up', 'except': ['npc_vask_porch']},
    fx('shadow', **{'from': 'shadow_from', 'to': 'shadow_to'}, time=3.6, alpha=0.6),
    sfx('gust', 1.0),
    face('wynn_door', d='up'),
    pose('wynn_door', 'shake'),
    wait(3.8),
    fade('clear', 1.2),
    {'do': 'crowd', 'release': True},
    {'do': 'remove', 'actor': 'wynn_door'},
    cam('npc_vask_porch', 1.2),
    pose('npc_vask_porch', 'fury'),
    {'do': 'tint', 'who': 'npc_vask_porch', 'colour': [255, 196, 182]},
    wait(0.8),
    {'do': 'tint', 'who': 'npc_vask_porch', 'colour': [255, 164, 148]},
    fx('steam', who='npc_vask_porch'),
    wait(0.9),
    fx('steam', who='npc_vask_porch'),
    wait(0.9),
    fx('steam', who='npc_vask_porch'),
    wait(1.6),
    fade('black', 2.4),
    flag('ACT1_DRAGON_SHADOW_SEEN'),
    wait(1.0),
    # The Act II card, right after the fade out, with no time skip.
    {'do': 'title', 'text': 'ACT II', 'sub': 'WWDD', 'time': 4.5},
    flag('ACT2_00_STARTED'),
    fade('clear', 1.6),
    *end_control(),
]
S.append({'id': 'act1_report', 'on': {'talk': 'npc_mayor_guild', 'map': 'guild_hall'},
          'when': when(['ACT1_MOSSVALE_VISITED', 'ACT1_FERNHOLLOW_VISITED'], ['ACT1_NEIGHBORS_REPORTED']), 'steps': [
    bars(True),
    cam('gh_cam', zoom=1.05),
    narrate('Mayor Hale and Guild Master Orlend stand at the long desk, a map of the region spread between them. They '
            'look up as you come in.'),
    say('npc_mayor_guild', 'Tell us everything.'),
    narrate("You tell them: Mossvale's sleeping lanes; the inn, and the red eye in the gloom; Apocolo's torn recipe "
            "book; the Dreamcatcher flaring and snapping back; Fernhollow's empty streets; the door lying flat; the "
            'ghostly struggle in the dream house.'),
    narrate("Mayor Hale's jaw tightens. The Guild Master studies the map in silence, then taps it twice."),
    say('npc_guildmaster', "(low) Two more towns, and a woman gone. That's no curse drifting down the road. That is a "
                           'hand reaching for each of them in turn.'),
    say('npc_mayor_guild', "(rubbing his eyes) And he watched you the whole way down, like a crow on a fence. "
                           "Havenbrook woke because you stood in the dark for us. Mossvale and Fernhollow have nobody "
                           "standing there for them. Not yet."),
    say('npc_guildmaster', "(to you) Not yet. Those torn pages belong to Apocolo, the apothecary Vexel took from us, "
                           "and his recipe may do what the Dreamcatcher can't. Find them. Whatever he wants, he isn't "
                           "finished. You've earned a moment to breathe."),
    fade('black', 1.6),
    flag('ACT1_NEIGHBORS_REPORTED'),
    music('', 0.6),
    {'do': 'map', 'map': 'town_havenbrook', 'spawn': 'from_guild_hall'},
    fade('clear', 1.2),
    *DRAGON_62,
]})
S.append({'id': 'act1_dragon', 'on': {'enter': 'town_havenbrook', 'spawn': 'from_guild_hall'},
          'when': when(['ACT1_NEIGHBORS_REPORTED'], ['ACT1_DRAGON_SHADOW_SEEN']),
          'steps': [bars(True, 0.0), *DRAGON_62]})


# ---------------------------------------------------------------------------------------------
#  ACT II -- WWDD (Screenplay.md, scenes 63-85)
# ---------------------------------------------------------------------------------------------
# --- 65: Torn Pages. Each page where its trace said; every second one, the rival.
PAGE_SITES = [
    ('graveyard', 'overworld', 'Wedged under a leaning headstone, weighted with a clod of grave dirt: a torn page in '
                               'a small, careful hand.'),
    ('lizard', 'dungeon_lizard_cave', 'Pinned under a shaman\'s bundle of bones and feathers: a torn page, a green '
                                      'scale caught in its fold.'),
    ('bear', 'dungeon_bear_den', 'Trodden into the bedding at the back of the den, among the bones: a torn page, '
                                 'furred with coarse brown hair.'),
    ('orc', 'dungeon_emberfell_2', 'In the warlord\'s chest, under a greasy rag: a torn page with a grey handprint '
                                   'across it.'),
]
for key, map_id, line in PAGE_SITES:
    up = key.upper()
    S.append({'id': 'act2_page_' + key, 'on': {'use': 'page_' + key, 'map': map_id},
              'when': when(['ACT1_OONA_BOOK_FOUND'], ['ACT1_PAGE_%s_FOUND' % up]), 'steps': [
        narrate(line),
        {'do': 'give', 'item': 'torn_page_' + key},
        flag('ACT1_PAGE_%s_FOUND' % up),
        count('ACT1_PAGES', 4),
    ]})
S.append({'id': 'act2_pages_all', 'on': {'flag': 'ACT1_PAGES_4'}, 'when': when([], ['ACT1_PAGES_ALL_FOUND']),
          'steps': [flag('ACT1_PAGES_ALL_FOUND')]})


# 65A: every second page, the rival: the colours flatten, a hooded figure steps
# out of nothing, and the player cannot lift a hand. The second visit has a
# voice, and a glimpse of a face.
def rival_visit(second):
    steps = [
        bars(True),
        fx('flash', colour=[188, 180, 214], amount=0.45),
        sfx('tear', 0.6, 1.2),
        narrate('You lift the torn page. The air goes cold and the colours flatten, as if the world had been dipped '
                'in dream.'),
        {'do': 'spawn', 'actor': 'rival', 'at': 'player', 'dx': 72, 'face': 'left', 'keep': True},
        {'do': 'tint', 'who': 'rival', 'colour': [86, 74, 104]},
        fx('appear', who='rival'),
        narrate('A hooded figure steps out of nothing: the same figure that stood in the Fernhollow window, solid, '
                'in full colour, face hidden. You reach for a weapon and cannot. Your arms will not obey.'),
        {'do': 'walk', 'who': 'rival', 'toward': 'player', 'dist': 44, 'speed': 320},
        sfx('impact', 1.0, 0.9),
        fx('flash', colour=[255, 255, 255], amount=0.5),
        {'do': 'player', 'pose': 'lie'},
        wait(0.5),
        sfx('impact', 0.8, 1.1),
        narrate('The figure crosses the distance in one stride and strikes. You are thrown down, struck again, and '
                'held to the ground.'),
    ]
    if second:
        steps += [
            {'do': 'tint', 'who': 'rival', 'colour': [255, 255, 255]},
            narrate('For a moment the hood falls back, and there is a face under it -- and the figure speaks, too '
                    'low to make out a word. Then the hood is up again.'),
        ]
    steps += [
        narrate('The figure looks at the page in your fist, and leaves it there.'),
        fx('vanish', who='rival'),
        wait(0.9),
        {'do': 'remove', 'actor': 'rival'},
        fx('flash', colour=[255, 255, 255], amount=0.3),
        narrate('The figure steps back into a ripple in the air, and the Reverie closes over it. The colours snap '
                'back. The page is still in your hand.'),
        {'do': 'player', 'pose': ''},
        {'do': 'rest'},
        count('ACT2_RIVAL_VISITS', 2),
        *end_control(),
    ]
    return steps


S.append({'id': 'act2_rival_visit_1', 'on': {'flag': 'ACT1_PAGES_2'}, 'when': when([], ['ACT2_RIVAL_VISITS_1']),
          'steps': rival_visit(False)})
S.append({'id': 'act2_rival_visit_2', 'on': {'flag': 'ACT1_PAGES_4'}, 'when': when([], ['ACT2_RIVAL_VISITS_2']),
          'steps': rival_visit(True)})

# --- the book, on Oona's bench: how many pages, and mended once they are all found.
for k in range(4):
    have = ['ACT1_PAGES_%d' % k] if k > 0 else []
    S.append({'id': 'act2_book_%d' % k, 'on': {'use': 'apocolo_book', 'map': 'mossvale_herbalist'},
              'when': when(['ACT1_OONA_BOOK_FOUND'] + have, ['ACT1_PAGES_%d' % (k + 1)]),
              'steps': [narrate("Apocolo's recipe book. %s The cure is on the pages that are gone."
                                % ['Four pages torn out, their stubs like broken teeth.',
                                   'One torn page found; three stubs still empty.',
                                   'Two torn pages found; two stubs still empty.',
                                   'Three torn pages found; one stub still empty.'][k])]})
S.append({'id': 'act2_book_mend', 'on': {'use': 'apocolo_book', 'map': 'mossvale_herbalist'},
          'when': when(['ACT1_PAGES_ALL_FOUND'], ['ACT2_BOOK_MENDED']), 'steps': [
    bars(True),
    cam('apocolo_book_at', 0.6),
    narrate('You set the four torn pages against the stubs in Apocolo\'s book. They knit into the spine as if they '
            'had never been torn.'),
    *[{'do': 'take', 'item': 'torn_page_' + k} for k in ('graveyard', 'lizard', 'bear', 'orc')],
    {'do': 'give', 'item': 'apocolo_recipe'},
    narrate('The cure is whole: a recipe in Apocolo\'s hand, to be brewed at the cauldron he kept in Havenbrook.'),
    flag('ACT2_BOOK_MENDED'),
    *end_control(),
]})
S.append({'id': 'act2_book_whole', 'on': {'use': 'apocolo_book', 'map': 'mossvale_herbalist'},
          'when': when(['ACT2_BOOK_MENDED'], []),
          'steps': [narrate("Apocolo's book, whole again.")]})

# --- 67: Apocolo's cauldron.
CURE_DOSES = 12
S.append({'id': 'act2_cauldron_cold', 'on': {'use': 'apocolo_cauldron', 'map': 'town_havenbrook'},
          'when': when([], ['ACT2_BOOK_MENDED']),
          'steps': [narrate("Apocolo's cauldron, stone cold since Vexel took him. A ring of old ash beneath it.")]})
S.append({'id': 'act2_cure', 'on': {'use': 'apocolo_cauldron', 'map': 'town_havenbrook'},
          'needs': 'apocolo_recipe', 'when': when(['ACT2_BOOK_MENDED'], ['ACT2_CURE_BREWED']), 'steps': [
    bars(True),
    cam('apocolo_cauldron_at', 0.6),
    narrate('The cauldron stands cold, a ring of ash beneath it. You light the fire and follow the recipe in '
            "Apocolo's hand, ingredient by ingredient."),
    sfx('burn', 0.6, 0.8),
    wait(0.8),
    fx('flash', colour=[255, 226, 140], amount=0.4),
    narrate('The murky brew clears to a pale gold, and a row of vials fills one by one.'),
    {'do': 'give', 'item': 'sleepers_cure', 'qty': CURE_DOSES},
    flag('ACT2_CURE_BREWED'),
    *end_control(),
]})
S.append({'id': 'act2_cure_more', 'on': {'use': 'apocolo_cauldron', 'map': 'town_havenbrook'},
          'needs': 'apocolo_recipe', 'when': when(['ACT2_CURE_BREWED'], ['ACT2_COLLEGE_WOKEN']), 'steps': [
    narrate("You brew another batch from Apocolo's recipe. The vials fill one by one."),
    {'do': 'give', 'item': 'sleepers_cure', 'qty': CURE_DOSES},
]})


# --- 68, 69: a dose a sleeper. Oona and Wendel have their own words.
def woken(npc, lines):
    return {'id': 'act2_cure_' + npc[4:], 'on': {'talk': npc, 'map': HOME[npc]}, 'needs': 'sleepers_cure',
            'when': when(['PROLOGUE', 'ACT2_CURE_BREWED'], ['CURED_' + npc, 'NEIGHBORS_SKIPPED']),
            'steps': [bars(True), {'do': 'take', 'item': 'sleepers_cure'}, *lines, flag('CURED_' + npc),
                      *end_control()]}


GENERIC = [narrate('You tip a few golden drops between their lips. A breath, a cough, and they sit up, blinking.')]
OONA = [
    narrate('You give a dose to Oona, and she wakes on her stool with the ladle still in her hand. Her pot has '
            'boiled down to a glowing syrup. She sees you, and then she sees the book on the bench, whole again.'),
    say('npc_oona', "(hoarse) That's Apocolo's hand. Those pages... you found all of them?"),
    narrate('You nod. Oona runs a thumb along the mended spine.'),
    say('npc_oona', "He taught me everything I know, and Vexel took him. If this book is whole, then so is a piece "
                    "of him. Come back to me, and I'll teach you what he taught me."),
]
WENDEL = [
    narrate('You crouch beside old Wendel in his chair by the window and tip the potion between his lips. He jerks '
            'awake, fishing line still wound around his fingers.'),
    say('npc_wendel_home', '(blinking) Did I... did something bite?'),
]
MOSSVALE_SLEEPERS = ['npc_sela', 'npc_pell', 'npc_tamsin', 'npc_oona', 'npc_hadley', 'npc_mv_patron_1',
                     'npc_mv_patron_2', 'npc_mv_carter', 'npc_mv_apple']
FERNHOLLOW_SLEEPERS = ['npc_maud', 'npc_pim', 'npc_college_porter', 'npc_mira', 'npc_wendel_home', 'npc_hesper',
                       'npc_nell', 'npc_ilse']
CURE_SCENES = [woken(n, OONA if n == 'npc_oona' else WENDEL if n == 'npc_wendel_home' else GENERIC)
               for n in MOSSVALE_SLEEPERS + FERNHOLLOW_SLEEPERS]
# Before the Dreamcatcher's block, which answers the same talk: a sleeper with a
# dose to hand is woken, not pushed back at.
first_block = next(i for i, sc in enumerate(S) if sc['id'].startswith('act1_blocked_'))
S[first_block:first_block] = CURE_SCENES
for town, sleepers, done in (('mossvale', MOSSVALE_SLEEPERS, 'ACT2_MOSSVALE_WOKEN'),
                             ('fernhollow', FERNHOLLOW_SLEEPERS, 'ACT2_FERNHOLLOW_WOKEN')):
    for n in sleepers:
        S.append({'id': 'act2_%s_woken_%s' % (town, n[4:]), 'on': {'flag': 'CURED_' + n},
                  'when': when(['CURED_' + m for m in sleepers], [done]), 'steps': [
            flag(done),
            narrate({'mossvale': 'Down the lanes, villagers wake by the well and in the doorways, rubbing their eyes. '
                                 'The wind chimes begin to ring.',
                     'fernhollow': 'Across the hamlet, dose by dose, people wake, and the lanterns that burned '
                                   'through the night gutter out. At the south-east wall, a neighbour stands in front '
                                   'of a broken door and looks inside. Nobody comes out.'}[town]),
        ]})
for n in ('ACT2_MOSSVALE_WOKEN', 'ACT2_FERNHOLLOW_WOKEN'):
    S.append({'id': 'act2_neighbors_woken_' + n.split('_')[1].lower(), 'on': {'flag': n},
              'when': when(['ACT2_MOSSVALE_WOKEN', 'ACT2_FERNHOLLOW_WOKEN'], ['ACT2_NEIGHBORS_WOKEN']),
              'steps': [flag('ACT2_NEIGHBORS_WOKEN')]})
# Oona's lesson: brewing, as Apocolo taught her.
S.append({'id': 'act2_oona_lesson', 'on': {'talk': 'npc_oona', 'map': 'mossvale_herbalist'},
          'when': when(['CURED_npc_oona'], ['ACT2_OONA_TEACHES']), 'steps': [
    bars(True),
    narrate('Oona teaches you to brew, the way Apocolo taught her.'),
    flag('ACT2_OONA_TEACHES'),
    *end_control(),
]})

# --- 66: Wynn, behind her Mossvale counter.
S.append({'id': 'act2_wynn_found', 'on': {'enter': 'mossvale_weavers'},
          'when': when(['PROLOGUE', 'ACT1_DRAGON_SHADOW_SEEN'], ['ACT2_CLOTHIER_FOUND']), 'steps': [
    bars(True, 0.0),
    cam('npc_wynn'),
    wait(0.5),
    narrate('You lean over the counter. Wynn is awake, crouched on the floor with her arms wrapped around her knees, '
            'shaking. She flinches from you, then looks up and sees who it is.'),
    narrate('You kneel and wait, saying nothing, until her breathing slows.'),
    say('npc_wynn', "(unsteady) You're the one from Havenbrook. You came into my dream and cut me loose."),
    say('npc_wynn', "(a shaky breath) I thought I was past it. After that shadow crossed the sky I ran all the way "
                    "here, thinking Mossvale would be safer. But every time I close my eyes, it's that place again. "
                    "The thread. The humming."),
    say('npc_wynn', "When I reached Mossvale it was too quiet. Everyone was already down, in the lane, in doorways, at "
                    "the well. I ran home and hid."),
    say('npc_wynn', "(she glances at the window) Far off, past the trees, I heard drums, and a chant I didn't know. It "
                    "went on for hours. Then it stopped, and the quiet was worse. I did not dare leave my hiding place "
                    "until you came."),
    pose('npc_wynn', ''),
    say('npc_wynn', "(steadier, rising) I've been behind this counter ever since. Thank you for coming. My loom in "
                    "Havenbrook is yours for as long as you need it. Weave whatever you need."),
    flag('ACT2_CLOTHIER_FOUND'),
    *end_control(),
]})

# --- 68A/68B: the house in Mossvale, at the Mayor's.
S.append({'id': 'act2_house_offer', 'on': {'talk': 'npc_mayor', 'map': 'mayor_hall'},
          'when': when(['ACT2_HOUSE_ASKED_BESS'], ['ACT2_HOUSE_OFFERED']), 'steps': [
    bars(True),
    face('npc_mayor', 'player'),
    narrate('Mayor Hale looks up from his desk as you explain.'),
    say('npc_mayor', "A place to live? There's a house in Mossvale that would suit you well. Let me check the "
                     "documents."),
    narrate('Mayor Hale pulls a ledger from the shelf and runs a finger down the page, then looks up.'),
    say('npc_mayor', "Nobody is living there, and it's worth fifty thousand. But for the hero of this town, I'll take "
                     "twenty off. Thirty thousand, and the house is yours."),
    flag('ACT2_HOUSE_OFFERED'),
    quest('q_act2_mossvale_house'),
    *end_control(),
]})
S.append({'id': 'act2_house_buy', 'on': {'talk': 'npc_mayor', 'map': 'mayor_hall'},
          'needs': 'coins', 'needs_qty': 30000, 'when': when(['ACT2_HOUSE_OFFERED'], ['ACT2_HOUSE_BOUGHT']), 'steps': [
    {'do': 'confirm', 'text': 'Pay Mayor Hale thirty thousand coins for the house in Mossvale?', 'yes': 'Pay',
     'no': 'Not yet'},
    {'do': 'take', 'item': 'coins', 'qty': 30000},
    {'do': 'give', 'item': 'mossvale_house_key'},
    narrate('Mayor Hale counts the coins into his strongbox and hands you the key to the house in Mossvale.'),
    flag('ACT2_HOUSE_BOUGHT'),
]})
S.append({'id': 'act2_house_short', 'on': {'talk': 'npc_mayor', 'map': 'mayor_hall'},
          'when': when(['ACT2_HOUSE_OFFERED'], ['ACT2_HOUSE_BOUGHT']),
          'steps': [say('npc_mayor', 'Thirty thousand, and the house in Mossvale is yours. Come back when you have it.')]})


# ---------------------------------------------------------------------------------------------
#  Act II opens on the Guild Master: Orlend's ledger of named beasts (data/dialogue.json
#  guildmaster_ledger, q_guild_ledger and the q_guild_* bounties; the board is genmaps' act2).
#  Nothing of Act II is scripted yet; this is the card that says how the bounties work, once
#  the ledger is open.
# ---------------------------------------------------------------------------------------------
TIPS.update({
    'guild_bounties': {'title': 'Guild Bounties',
                       'text': "The board beside Orlend's desk posts a bounty on each of the named beasts, waking "
                               "and dreaming. Take one and the arrow leads to its lair -- though it counts wherever it "
                               "falls -- and the Guild pays the moment it does. Each is taken once. {Target} on "
                               "the board shows only those within ten levels of you."},
})
S.append({'id': 'act2_bounties_tip', 'on': {'flag': 'ACT2_GUILD_BOUNTIES_OPEN'},
          'when': when([], ['ACT2_TIP_BOUNTIES']),
          'steps': [flag('ACT2_TIP_BOUNTIES'), {'do': 'tip', 'id': 'guild_bounties'}]})


# ===== ACT II, part 2 (scenes 63, 70-85) =====
ACTORS.update({
    'orrin': {'sprite': 'magister', 'name': 'Magister Orrin'},
    'guard1': {'sprite': 'fighter2', 'name': 'Guard'},
    'guard2': {'sprite': 'fighter2', 'name': 'Guard'},
})
TIPS.update({
    'talisman': {'title': 'The Death Talisman',
                 'text': 'Wear the talisman in its own place: open your bag and put it on. Then {Talisman} steps '
                         'into the Reverie where you stand -- in Havenbrook, at the college, on the Ashen Path, at the '
                         "Plateau's stronghold or in the Bayou -- and {Talisman} again steps back out. It wants five "
                         'seconds between. It does nothing in the dreamworld a bed sends you to.'},
})
RIVAL_TINT = [150, 138, 176]
VASK = 'npc_vask_porch'

# --- 63: Vask, angry, until the player is strong enough. Each talk: one line,
# never the same twice running.
S.append({'id': 'act2_vask_not_now', 'on': {'talk': VASK, 'map': 'town_havenbrook'},
          'when': when(['ACT1_DRAGON_SHADOW_SEEN'], ['ACT2_ICESPIRE_READY', 'ACT2_VASK_NOT_NOW']), 'steps': [
    fx('steam', who=VASK),
    say(VASK, '(through his teeth, steam hissing) Not now, lad.'),
    flag('ACT2_VASK_NOT_NOW'),
]})
VASK_ROTATION = [
    ("(without looking at you) Don't ask me yet. You aren't ready to hear it.", [], ['ACT2_VASK_GRUMBLE_A', 'ACT2_VASK_GRUMBLE_B'],
     [flag('ACT2_VASK_GRUMBLE_A')]),
    ('(a hard puff of steam) Go on. Grow stronger. Ask me again when you can.', ['ACT2_VASK_GRUMBLE_A'], [],
     [{'do': 'flag', 'clear': 'ACT2_VASK_GRUMBLE_A'}, flag('ACT2_VASK_GRUMBLE_B')]),
    ("(a low growl) Hrrmph. That sky isn't empty anymore.", ['ACT2_VASK_GRUMBLE_B'], [],
     [{'do': 'flag', 'clear': 'ACT2_VASK_GRUMBLE_B'}]),
]
for k, (line, have, lack, after) in enumerate(VASK_ROTATION):
    S.append({'id': 'act2_vask_grumble_%d' % (k + 1), 'on': {'talk': VASK, 'map': 'town_havenbrook'},
              'when': when(['ACT1_DRAGON_SHADOW_SEEN', 'ACT2_VASK_NOT_NOW'] + have, ['ACT2_ICESPIRE_READY'] + lack),
              'steps': [fx('steam', who=VASK), say(VASK, line), *after]})
# Level 35: the red fades, the steam stops, and a marker hangs over him.
S.append({'id': 'act2_icespire_ready', 'on': {'level': 35},
          'when': when(['ACT1_DRAGON_SHADOW_SEEN'], ['ACT2_ICESPIRE_READY']), 'steps': [flag('ACT2_ICESPIRE_READY')]})

# --- 77: the dragon slayer.
S.append({'id': 'act2_vask_reveal', 'on': {'talk': VASK, 'map': 'town_havenbrook'},
          'when': when(['ACT2_ICESPIRE_READY'], ['ACT2_ICESPIRE_STARTED']), 'steps': [
    bars(True),
    cam('vask_porch_cam', 0.8),
    narrate('You climb the porch steps. Elder Vask sits in his rocker, and for the first time since the shadow crossed '
            'the sky the red has faded from his face and the steam has stopped. His hands rest white-knuckled on the '
            'arms of the chair, but his eyes are clear.'),
    say(VASK, "You're ready. I can see it in how you stand. Sit a moment, lad. There's a thing I've kept to myself "
              'too long.'),
    say(VASK, "(a long breath) I wasn't always a man in a rocking chair. In my warrior days I was a dragon slayer, and "
              'I took down every dragon that crossed my path. That shadow over the town was Hoarfang. I must take down '
              'this one just as I did the rest.'),
    {'do': 'confirm', 'text': '', 'yes': "Let me handle it. You've earned your rest.", 'no': 'Not yet'},
    say(VASK, "Strain myself? I was felling dragons before you could walk. Make no mistake, I'll be there."),
    pose(VASK, 'fist'),
    narrate('He grips the arms of the chair and pushes himself up. His knees tremble. He lowers himself back down, '
            'and the fight drains out of him.'),
    pose(VASK, ''),
    say(VASK, "...No. I'm not as nimble as I was. Slay the beast for me, adventurer, and bring me back its head."),
    {'do': 'flag', 'clear': 'ACT2_HOARFANG_SLAIN'},
    flag('ACT2_ICESPIRE_STARTED'),
    quest('q_act2_hoarfang'),
    *end_control(),
]})
# 78: the head, taken from the fallen dragon.
S.append({'id': 'act2_hoarfang_head', 'on': {'flag': 'ACT2_HOARFANG_SLAIN', 'map': 'ice_spire_peak'},
          'when': when(['ACT2_ICESPIRE_STARTED'], ['ACT2_HOARFANG_HEAD', 'ACT2_HOARFANG_RESOLVED']), 'steps': [
    narrate("You take Hoarfang's head from the fallen dragon."),
    {'do': 'give', 'item': 'hoarfang_head'},
    flag('ACT2_HOARFANG_HEAD'),
]})
# --- 78A: the head on the porch steps.
S.append({'id': 'act2_vask_head', 'on': {'talk': VASK, 'map': 'town_havenbrook'}, 'needs': 'hoarfang_head',
          'when': when(['ACT2_ICESPIRE_STARTED'], ['ACT2_HOARFANG_RESOLVED']), 'steps': [
    bars(True),
    cam('vask_porch_cam', 0.8),
    narrate('You set the great head down at the foot of the porch steps. Elder Vask stares at it. His eyes go wide.'),
    say(VASK, '(hoarsely) You... did it!'),
    narrate("He rises, unsteady, and rests a trembling hand on the dragon's brow."),
    say(VASK, "I haven't slain a dragon in twenty years of living in the Overworld, and you've made me feel like myself "
              'in my slayer days. I fought my struggles in the Reverie too, back in those days, and I thought those '
              'battles were behind me.'),
    say(VASK, '(he looks up) Thank you, adventurer. And thank you for carrying the fight into the Dreamworld as well.'),
    narrate('Vask lays five weapons on the porch rail, each crafted from Hoarfang: a pair of twin fang daggers, a scale '
            'bow, a staff, a greatsword, and a mace. He pushes the great head across to you, ready to mount on a '
            'mantle.'),
    say(VASK, 'The head is yours, for your mantle. And take your pick of these. Choose well.'),
    {'do': 'take', 'item': 'hoarfang_head'},
    flag('ACT2_HOARFANG_RESOLVED'),
    *end_control(),
]})
S.append({'id': 'act2_vask_waiting', 'on': {'talk': VASK, 'map': 'town_havenbrook'},
          'when': when(['ACT2_ICESPIRE_STARTED'], ['ACT2_HOARFANG_RESOLVED']),
          'steps': [say(VASK, 'Slay the beast for me, adventurer, and bring me back its head.')]})

# --- 74: Bess and the deep well.
S.append({'id': 'act2_bess_well', 'on': {'talk': 'npc_cook', 'map': 'house_inn'},
          'when': when(['ACT2_00_STARTED'], ['ACT2_BESS_WELL_START']), 'steps': [
    bars(True),
    cam('npc_cook', 0.6),
    narrate("Bess wipes down the bar and doesn't quite look at you."),
    say('npc_cook', "I wouldn't ask, but there's nobody else. The old deep well in the middle of town has clogged. The "
                    "water's stopped running, and at night something knocks from below. Clear it out for me, will you?"),
    flag('ACT2_BESS_WELL_START'),
    flag('recipe:lantern_unlit'),
    quest('q_dry_well'),
    *end_control(),
]})
# --- 75: the bottom of the well, and what guards its water.
S.append({'id': 'act2_well_bottom', 'on': {'near': 'well_spring_at', 'map': 'well_deep', 'radius': 280},
          'when': when(['ACT2_BESS_WELL_START'], ['ACT2_WELL_SEEN', 'ACT2_WELL_CLEARED']), 'steps': [
    bars(True),
    cam('well_spring_at', 0.8),
    narrate('A dry chamber opens around a sealed spring, its mouth choked with grey slime and dead roots. Crouched over '
            'it, one pale hand pressed to the stone where the water should run, is a gaunt, hollow-eyed figure in '
            'rotted burial cloth: the Thing in the Spring.'),
    narrate('It lifts its head. The hollow eyes find you.'),
    flag('ACT2_WELL_SEEN'),
    *end_control(),
]})
S.append({'id': 'act2_well_cleared', 'on': {'flag': 'ACT2_WELL_WARDEN_DOWN', 'map': 'well_deep'},
          'when': when(['ACT2_BESS_WELL_START'], ['ACT2_WELL_CLEARED']), 'steps': [
    narrate('The Thing in the Spring collapses into dust.'),
    flag('ACT2_WELL_CLEARED'),
]})

# --- 70: the college courtyard. The rival, Vexel on the steps, and Orrin in his thread.
S.append({'id': 'act2_college_quest', 'on': {'flag': 'ACT2_NEIGHBORS_WOKEN'},
          'when': when(['PROLOGUE'], ['ACT2_COLLEGE_QUEST']),
          'steps': [flag('ACT2_COLLEGE_QUEST'), quest('q_act2_college')]})
S.append({'id': 'act2_college_fight', 'on': {'enter': 'college_grounds'},
          'when': when(['ACT2_NEIGHBORS_WOKEN'], ['ACT2_RIVAL_FIGHT']), 'steps': [
    bars(True, 0.0),
    cam('cg_court'),
    narrate('You step through the college gate into the courtyard. A dry fountain stands at its heart, and ivy chokes '
            'the cloisters. Masters and students lie asleep along the walks, as if felled mid-step. The potion has not '
            'reached them.'),
    cam('cg_steps', 1.8),
    narrate("At the top of the far steps Vexel Von Finch waits with his long fingers folded. Beside him an old mage in "
            "a grey robe, the head of the college, Magister Orrin, hangs asleep in a net of black thread: Vexel's "
            'hostage. On his other side stands the rival Dreamwalker, hood thrown back. It is the figure from the '
            'Fernhollow window, and from the visits.'),
    narrate('The rival walks down the steps alone. Vexel does not move. You, free to act at last, draw.'),
    music('boss', 1.0),
    flag('ACT2_RIVAL_FIGHT'),
    *end_control(),
]})
S.append({'id': 'act2_rival_down', 'on': {'flag': 'ACT2_RIVAL_DEFEATED', 'map': 'college_grounds'},
          'when': when(['ACT2_RIVAL_FIGHT'], ['ACT2_RIVAL_SPARED', 'ACT2_RIVAL_CAPTURED']), 'steps': [
    bars(True),
    music('', 1.2),
    narrate('At last the rival drops to a knee, then falls. The courtyard goes quiet.'),
    fade('black', 0.5),
    {'do': 'banish', 'type': 'rival_dreamwalker'},
    {'do': 'spawn', 'actor': 'rival', 'at': 'player', 'dx': 52, 'face': 'left', 'keep': True},
    {'do': 'tint', 'who': 'rival', 'colour': RIVAL_TINT},
    pose('rival', 'lie'),
    fade('clear', 0.6),
    cam('cg_steps', 1.0),
    narrate('Vexel looks down at the fallen rival and grimaces, as he did at you in Mossvale. He does not step '
            'forward. He does not reach out.'),
    fx('flash', colour=[180, 120, 255], amount=0.6),
    sfx('vanish', 0.8),
    {'do': 'banish', 'type': 'vexel_beams'},
    narrate('A violet flash, and he is gone, leaving the hostage hanging in his thread.'),
    cam('rival', 0.8),
    narrate('The rival raises a hand toward the empty steps.'),
    say('rival', "(hoarse) Wait. You said you'd come back for me."),
    narrate('The hand drops. The rival does not rise.'),
    narrate('You walk to the fallen rival and stand over them.'),
    {'do': 'confirm', 'text': '', 'yes': 'Spare and recruit', 'no': 'Capture', 'choice': True,
     'flag': 'ACT2_RIVAL_SPARED', 'no_flag': 'ACT2_RIVAL_CAPTURED'},
    narrate('You lower your weapon and offer a hand. The rival stares at the empty steps where Vexel stood. Then, '
            'slowly, the rival takes the hand.'),
    pose('rival', 'sit_up'),
    wait(0.9),
    pose('rival', ''),
    say('rival', "He wasn't coming back, was he. ...Then I'll fight beside you, for now."),
    fx('vanish', who='rival'),
    wait(0.9),
    {'do': 'remove', 'actor': 'rival'},
    flag('ACT2_RIVAL_FATE'),
    *end_control(),
]})
S.append({'id': 'act2_rival_captured', 'on': {'flag': 'ACT2_RIVAL_CAPTURED', 'map': 'college_grounds'},
          'when': when([], ['ACT2_RIVAL_FATE']), 'steps': [
    bars(True, 0.0),
    {'do': 'remove', 'actor': 'rival'},
    {'do': 'spawn', 'actor': 'rival', 'at': 'player', 'dx': 52, 'face': 'left', 'keep': True},
    {'do': 'tint', 'who': 'rival', 'colour': RIVAL_TINT},
    pose('rival', 'lie'),
    narrate("You bind the rival's wrists. The rival does not resist, eyes still on the steps where Vexel stood."),
    fade('black', 1.2),
    {'do': 'remove', 'actor': 'rival'},
    {'do': 'map', 'map': 'havenbrook_cells', 'at': 'cells_cam'},
    {'do': 'player', 'hidden': True},
    cam('cells_cam'),
    {'do': 'spawn', 'actor': 'guard1', 'at': 'cells_stair', 'face': 'right', 'keep': True},
    {'do': 'spawn', 'actor': 'rival', 'at': 'cells_stair', 'dx': -34, 'face': 'right', 'keep': True},
    {'do': 'tint', 'who': 'rival', 'colour': RIVAL_TINT},
    {'do': 'spawn', 'actor': 'guard2', 'at': 'cells_stair', 'dx': -68, 'face': 'right', 'keep': True},
    fade('clear', 1.0),
    narrate('Later. In Havenbrook, guards lead the rival down into the dungeon, and a heavy door closes.'),
    walk('guard1', 'cells_cell', 44, False, dx=36),
    walk('rival', 'cells_cell', 44, False),
    walk('guard2', 'cells_cell', 44, True, dx=-36),
    {'do': 'remove', 'actor': 'rival'},
    sfx('door', 1.0, 0.6),
    wait(1.0),
    fade('black', 1.0),
    *[{'do': 'remove', 'actor': a} for a in ('guard1', 'guard2')],
    {'do': 'player', 'hidden': False},
    flag('ACT2_RIVAL_FATE'),
    {'do': 'map', 'map': 'college_grounds', 'at': 'cg_after'},
    cam('player'),
    fade('clear', 1.0),
    *end_control(),
]})


# --- 71: Orrin wakes, and the first lesson.
def lesson(who):
    return [
        narrate('You nod. The old mage traces a single glowing rune in the air above your open hands. It settles into '
                'them, warm and steady.'),
        fx('flash', colour=[200, 170, 255], amount=0.4),
        sfx('chime', 0.8, 0.9),
        say(who, 'A first lesson, no more. Any staff you carry will hold the old magic now. The rest I will teach '
                 'when my council and my staff are awake.'),
        flag('recipe:spell:eldritch_blast'),
        flag('ACT2_ANCIENT_SLOT_UNLOCKED'),
        quest('q_act2_wake_college'),
    ]


S.append({'id': 'act2_orrin_wakes', 'on': {'flag': 'ACT2_RIVAL_FATE', 'map': 'college_grounds'},
          'when': when([], ['ACT2_COLLEGE_HEAD_AWAKE']), 'steps': [
    bars(True),
    pose('npc_orrin_court', ''),
    cam('npc_orrin_court', 1.0),
    narrate('The net of black thread around the old mage unravels strand by strand, and he drops lightly to the '
            'flagstones. Magister Orrin draws a long, shuddering breath and opens his eyes.'),
    flag('ACT2_ORRIN_FREED'),
    say('npc_orrin_court', 'He held me up like a lantern and made me watch. ...You stood in this courtyard against both '
                           'of them. The college owes you more than it can pay.'),
    say('npc_orrin_court', '(he looks along the cloisters at the sleeping masters and students) Most of mine still '
                           'sleep: the mage council in the great hall, the staff in the other chambers, and the rest '
                           'of my students. Help me wake them, and I will teach you the arts of the ancient magics.'),
    flag('ACT2_COLLEGE_HEAD_AWAKE'),
    {'do': 'confirm', 'text': '', 'yes': 'Agree', 'no': 'Not yet'},
    *lesson('npc_orrin_court'),
    *end_control(),
]})
S.append({'id': 'act2_orrin_offer', 'on': {'talk': 'npc_magister', 'map': 'fernhollow_college'},
          'when': when(['ACT2_COLLEGE_HEAD_AWAKE'], ['ACT2_ANCIENT_SLOT_UNLOCKED']), 'steps': [
    bars(True),
    say('npc_magister', 'Help me wake them, and I will teach you the arts of the ancient magics.'),
    {'do': 'confirm', 'text': '', 'yes': 'Agree', 'no': 'Not yet'},
    *lesson('npc_magister'),
    *end_control(),
]})

# Wake the College: a dose a sleeper -- the council, the staff, the students.
COUNCIL = ['npc_councillor_ferris', 'npc_councillor_wren']
STAFF = ['npc_college_instructor', 'npc_college_lector']
STUDENTS = ['npc_college_walker_a', 'npc_college_walker_b', 'npc_college_reader', 'npc_college_gardener',
            'npc_college_usher'] + ['npc_college_lane_%d' % k for k in range(4)] + \
           ['npc_college_pupil_%d' % k for k in range(5)]
COLLEGE_CURES = [{'id': 'act2_cure_' + n[4:], 'on': {'talk': n, 'map': HOME[n]}, 'needs': 'sleepers_cure',
                  'when': when(['PROLOGUE', 'ACT2_COLLEGE_HEAD_AWAKE'], ['CURED_' + n]),
                  'steps': [bars(True), {'do': 'take', 'item': 'sleepers_cure'}, *GENERIC, flag('CURED_' + n),
                            *end_control()]}
                 for n in COUNCIL + STAFF + STUDENTS]
first_talk = next(i for i, sc in enumerate(S) if sc['id'] == 'act2_orrin_offer')
S[first_talk:first_talk] = COLLEGE_CURES
for group, done in ((COUNCIL, 'ACT2_COLLEGE_COUNCIL_WOKEN'), (STAFF, 'ACT2_COLLEGE_STAFF_WOKEN'),
                    (STUDENTS, 'ACT2_COLLEGE_STUDENTS_WOKEN')):
    for n in group:
        S.append({'id': 'act2_%s_%s' % (done.split('_')[2].lower(), n[4:]), 'on': {'flag': 'CURED_' + n},
                  'when': when(['CURED_' + m for m in group], [done]), 'steps': [flag(done)]})
for f, other in (('ACT2_COLLEGE_COUNCIL_WOKEN', 'ACT2_COLLEGE_STAFF_WOKEN'),
                 ('ACT2_COLLEGE_STAFF_WOKEN', 'ACT2_COLLEGE_COUNCIL_WOKEN')):
    S.append({'id': 'act2_magics_open_' + f.split('_')[2].lower(), 'on': {'flag': f},
              'when': when([other], ['ACT2_ANCIENT_MAGICS_OPEN']), 'steps': [flag('ACT2_ANCIENT_MAGICS_OPEN')]})
GROUPS = ['ACT2_COLLEGE_COUNCIL_WOKEN', 'ACT2_COLLEGE_STAFF_WOKEN', 'ACT2_COLLEGE_STUDENTS_WOKEN']
for f in GROUPS:
    S.append({'id': 'act2_college_woken_' + f.split('_')[2].lower(), 'on': {'flag': f},
              'when': when(GROUPS, ['ACT2_COLLEGE_WOKEN']), 'steps': [flag('ACT2_COLLEGE_WOKEN')]})

# --- 71A: the council rises, and points at the Ashen Path.
S.append({'id': 'act2_council_wakes', 'on': {'flag': 'ACT2_COLLEGE_COUNCIL_WOKEN', 'map': 'fernhollow_college'},
          'when': when([], ['ACT2_ASHEN_PATH_SUGGESTED']), 'steps': [
    bars(True),
    cam('fc_cam', 0.8),
    narrate('The mage council, a ring of robed elders including Councillor Ferris and Councillor Wren, rises slowly '
            'from their seats, blinking in the daylight. Magister Orrin stands among them and takes in you and the '
            'open doors of the hall.'),
    say('npc_magister', 'We owe you our waking. Whatever Vexel Von Finch is doing, the traces of his magic lead toward '
                        'the Ashen Path. If you would repay us, go and investigate it for any sign of him.'),
    flag('ACT2_ASHEN_PATH_SUGGESTED'),
    flag('ACT2_ASHEN_PATH_OPEN'),
    quest('q_act2_ashen_path'),
    *end_control(),
]})

# --- 85, and its way in: the council's meeting about the rune. Before the archive
# (72): with the rune brought back, that is the conversation the Magister has.
def meeting(fade_in):
    steps = [bars(True)]
    if fade_in:
        steps += [fade('black', 1.2), {'do': 'map', 'map': 'fernhollow_college', 'at': 'fc_meeting'}]
    else:
        steps += [fade('black', 1.0), {'do': 'player', 'at': 'fc_meeting', 'face': 'up'}]
    steps += [
        cam('fc_cam'),
        fade('clear', 1.2),
        narrate('You sit in a discussion with the mage council and the head of the college in the big meeting hall. '
                'The council has heard everything.'),
        say('npc_magister', 'The college has studied your rune. It opens a portal into the Reverie. And someone has '
                            'been toying with the species of the overworld, setting them to attack in the Reverie. The '
                            'Nightmares you fought in the pit were only the beginning.'),
        say('npc_councillor_ferris', 'Destroy it. A door into the Reverie in the heart of the Ashen Land is a door for '
                                     'whatever is toying with these creatures to walk through.'),
        say('npc_councillor_wren', 'Or we experiment with it. A door that opens one way opens the other, and we would '
                                   'see our enemy before he sees us.'),
        say('npc_magister', 'Enough. You carried the rune back to us, and the choice is yours. Decide, and the college '
                            'will abide by it.'),
        {'do': 'confirm', 'text': '', 'yes': 'Destroy the rune', 'no': 'Experiment with the rune', 'choice': True,
         'flag': 'ACT2_RUNE_DESTROYED', 'no_flag': 'ACT2_RUNE_EXPERIMENT'},
    ]
    return steps


S.append({'id': 'act2_meeting_asked', 'on': {'talk': 'npc_magister', 'map': 'fernhollow_college'},
          'when': when(['ACT2_PALM_HEALED', 'ACT2_ANCIENT_MAGICS_OPEN'],
                       ['ACT2_RUNE_DESTROYED', 'ACT2_RUNE_EXPERIMENT']),
          'steps': meeting(False)})
for f in ('ACT2_RUNE_DESTROYED', 'ACT2_RUNE_EXPERIMENT'):
    S.append({'id': 'act2_meeting_end_' + f.split('_')[2].lower(), 'on': {'flag': f},
              'when': when([], ['ACT2_HEADMASTER_TOLD']), 'steps': [
        bars(True, 0.0),
        flag('ACT2_RUNE_FATE'),
        {'do': 'take', 'item': 'rune_rubbing'},
        narrate('The council murmurs. The meeting draws to its end.'),
        flag('ACT2_HEADMASTER_TOLD'),
        fade('black', 2.0),
        {'do': 'title', 'text': 'ACT III', 'sub': 'Attack on the Reverie', 'time': 4.5},
        flag('ACT3_00_STARTED'),
        fade('clear', 1.6),
        *end_control(),
    ]})

# --- 72: the archive, and the death talisman.
S.append({'id': 'act2_archive', 'on': {'talk': 'npc_magister', 'map': 'fernhollow_college'},
          'when': when(['ACT2_ANCIENT_MAGICS_OPEN'], ['ACT2_TALISMAN_SEARCH_START']), 'steps': [
    bars(True),
    cam('npc_magister', 0.6),
    narrate('The old mage climbs down from a ladder among the archive shelves, dust on his robe and a heavy book '
            'under one arm.'),
    say('npc_magister', 'Look at what the Reverie costs you: a bed, a sleeper, a Dreamcatcher, and always a door that '
                        'someone else holds open. The old Dreamwalkers carried a talisman that opened the veil for '
                        'them, in and out, as they pleased.'),
    say('npc_magister', 'This college teaches that the closest thing to dreaming is death, and death has a guardian. '
                        'My search points to the crypt beneath the cemetery. Deep down, the Ashlord, a vampire lord, '
                        'stands guard over a chest, and I believe the death talisman is inside. Go when you are strong '
                        'enough, and not before: the lower crypt is no place for the weak.'),
    flag('ACT2_TALISMAN_SEARCH_START'),
    quest('q_act2_death_talisman'),
    *end_control(),
]})

# --- 73: the bottom of the crypt.
S.append({'id': 'act2_ashlord_seen', 'on': {'near': 'cr3_vault', 'map': 'crypt_3', 'radius': 240},
          'when': when(['ACT2_TALISMAN_SEARCH_START'], ['ACT2_ASHLORD_SEEN', 'ACT2_ASHLORD_DEFEATED']), 'steps': [
    bars(True),
    cam('cr3_vault', 0.8),
    narrate('At the very bottom, in a round chamber of black stone, a tall, pale figure in a long, tattered coat stands '
            'motionless before a stone chest, the points of its fangs just showing. Ash drifts from its shoulders, and '
            'its eyes are two dull embers: the Ashlord, a vampire lord.'),
    narrate('As you step in, the embers flare. The Ashlord lifts its weapon.'),
    flag('ACT2_ASHLORD_SEEN'),
    *end_control(),
]})
S.append({'id': 'act2_ashlord_ash', 'on': {'flag': 'ACT2_ASHLORD_DEFEATED', 'map': 'crypt_3'},
          'when': when(['ACT2_TALISMAN_SEARCH_START'], ['ACT2_TALISMAN_FOUND', 'ACT2_ASHLORD_ASH']), 'steps': [
    narrate('The Ashlord crumbles into a heap of ash. The chest lid creaks open.'),
    flag('ACT2_ASHLORD_ASH'),
]})
S.append({'id': 'act2_talisman', 'on': {'use': 'talisman_chest', 'map': 'crypt_3'},
          'when': when(['ACT2_TALISMAN_SEARCH_START'], ['ACT2_TALISMAN_FOUND']), 'steps': [
    bars(True),
    narrate('Inside, wrapped in cloth, lies the death talisman. You lift it. It is warm to the touch, and it hums.'),
    {'do': 'give', 'item': 'death_talisman'},
    flag('ACT2_TALISMAN_FOUND'),
    {'do': 'tip', 'id': 'talisman'},
    *end_control(),
]})

# --- 80-81: the Pit Lord down; the Cinder King's warning; the rune.
CINDER = "TAKE THIS AS A WARNING, DREAMWALKER. YOU WILL BE SQUASHED THE MOMENT YOU WALK THROUGH MY DOORS. RETURN FROM WHENCE YOU CAME."
S.append({'id': 'act2_cinder_warning', 'on': {'flag': 'ACT2_PIT_LORD_DEFEATED', 'map': 'dungeon_infernal'},
          'when': when(['ACT2_ASHEN_PATH_SUGGESTED'], ['ACT2_CINDER_WARNING']), 'steps': [
    bars(True),
    music('', 1.0),
    narrate('You stand over the fallen Pit Lord in the burning depths. The pit goes quiet.'),
    fx('quake', amount=0.7, shake=0.5),
    narrate('Then the ground trembles. A voice rolls up from everywhere at once, from the rock, the ash, and the sky, '
            'as if the whole Ashen Land were speaking.'),
    fx('quake', amount=1.0, shake=0.9, volume=0.7),
    # A voice with no body: named, and nobody stands there to say it.
    {'do': 'say', 'name': 'Cinder King', 'text': '(from beyond; booming) ' + CINDER},
    fx('quake', amount=0.5, shake=0.4, volume=0.4),
    narrate('The rumbling fades. The shaking ends.'),
    flag('ACT2_CINDER_WARNING'),
    *end_control(),
]})
S.append({'id': 'act2_pit_rune', 'on': {'use': 'pit_rune', 'map': 'dungeon_infernal'},
          'when': when(['ACT2_CINDER_WARNING'], ['ACT2_RUNE_FOUND']), 'steps': [
    bars(True),
    cam('pit_rune_at', 0.6),
    narrate("You search the Pit Lord's dwelling, a black-stone hall of bones and cinders at the pit's heart. Behind a "
            'throne of rubble, a rune is carved into the wall. It glows with a pale, steady light unlike the hellfire '
            'around it.'),
    narrate('You touch it. The air in front of it folds open into a shimmering doorway, and through it lies the violet '
            'hush of the Reverie. You let it close.'),
    fx('dream'),
    wait(0.6),
    narrate('Someone at the Magic college has to hear of this.'),
    {'do': 'give', 'item': 'rune_rubbing'},
    flag('ACT2_RUNE_FOUND'),
    *end_control(),
]})

# --- 82-83: out of the Ashen Path, and the road to Fernhollow.
S.append({'id': 'act2_escape', 'on': {'enter': 'ashen_path', 'spawn': 'from_pit'},
          'when': when(['ACT2_RUNE_FOUND'], ['ACT2_ESCAPE_ACTIVE', 'ACT2_ASHEN_ESCAPE_DONE']), 'steps': [
    bars(True, 0.0),
    narrate('Demons close in from every ridge and ash-drift. You fight through them, step by step, along the road that '
            'led you in.'),
    flag('ACT2_ESCAPE_ACTIVE'),
    music('boss', 1.0),
    *end_control(),
]})
S.append({'id': 'act2_way_out', 'on': {'near': 'ap_escape_exit', 'map': 'ashen_path', 'radius': 200},
          'when': when(['ACT2_ESCAPE_ACTIVE'], ['ACT2_ASHEN_ESCAPE_DONE']), 'then': 'act2_palm', 'steps': [
    bars(True),
    music('', 1.5),
    flag('ACT2_ASHEN_ESCAPE_DONE'),
    {'do': 'flag', 'clear': 'ACT2_ESCAPE_ACTIVE'},
    narrate('The road finally gives way to ordinary earth. You stumble out of the ashen haze, armour scorched, steps '
            'dragging.'),
    fade('black', 1.2),
    {'do': 'map', 'map': 'fernhollow', 'at': 'fh_post'},
    {'do': 'player', 'pose': 'hurt', 'face': 'left'},
    cam('player'),
    fade('clear', 0.8),
    narrate('The long road to Fernhollow passes in pieces: you sagging against a post, then a wall, then pulling '
            'yourself up the last steps to the college gate.'),
    fade('black', 1.0),
    {'do': 'map', 'map': 'college_grounds', 'at': 'cg_gate_in'},
    {'do': 'player', 'pose': 'lie', 'face': 'up'},
    cam('player'),
    fade('clear', 0.8),
    narrate('At the door, your legs give out, and you collapse.'),
]})
# Into the college with the rune by any other road: battered all the same.
S.append({'id': 'act2_way_in', 'on': {'enter': 'college_grounds'},
          'when': when(['ACT2_RUNE_FOUND'], ['ACT2_PALM_HEALED', 'ACT2_ASHEN_ESCAPE_DONE']), 'then': 'act2_palm',
          'steps': [
    bars(True, 0.0),
    flag('ACT2_ASHEN_ESCAPE_DONE'),
    {'do': 'flag', 'clear': 'ACT2_ESCAPE_ACTIVE'},
    {'do': 'player', 'pose': 'lie', 'face': 'up'},
    narrate('At the door, your legs give out, and you collapse.'),
]})
# --- 84: the Healing Palm.
S.append({'id': 'act2_palm', 'on': {'flag': 'ACT2_PALM_NEVER'}, 'when': when([], ['ACT2_PALM_HEALED']), 'steps': [
    {'do': 'player', 'pose': 'lie'},
    {'do': 'spawn', 'actor': 'orrin', 'at': 'cg_steps', 'face': 'down', 'keep': True},
    walk('orrin', 'player', 70, True, dx=30),
    narrate('The head of the college crosses the courtyard, sees the fallen figure, and hurries over. He kneels and '
            'presses a glowing palm to your chest. Warm light spreads through you, and the colour returns to your '
            'face.'),
    fx('flash', colour=[255, 236, 190], amount=0.5),
    sfx('chime', 0.8, 1.1),
    {'do': 'rest'},
    {'do': 'player', 'pose': ''},
    face('orrin', 'player'),
    say('orrin', 'My dear boy! What has happened to you? Tell me everything, and leave nothing out.'),
    flag('ACT2_PALM_HEALED'),
]})
S.append({'id': 'act2_palm_meeting', 'on': {'flag': 'ACT2_PALM_HEALED', 'map': 'college_grounds'},
          'when': when(['ACT2_ANCIENT_MAGICS_OPEN'], ['ACT2_RUNE_DESTROYED', 'ACT2_RUNE_EXPERIMENT', 'ACT2_PALM_SOFTLOCK']),
          'steps': meeting(True)})
S.append({'id': 'act2_palm_softlock', 'on': {'flag': 'ACT2_PALM_HEALED', 'map': 'college_grounds'},
          'when': when([], ['ACT2_ANCIENT_MAGICS_OPEN', 'ACT2_PALM_SOFTLOCK']), 'steps': [
    {'do': 'remove', 'actor': 'orrin'},
    {'do': 'spawn', 'actor': 'orrin', 'at': 'player', 'dx': 30, 'face': 'left', 'keep': True},
    say('orrin', 'The council cannot hear you while they sleep. Wake them, and the staff with them, and I will gather '
                 'everyone in the great hall.'),
    flag('ACT2_PALM_SOFTLOCK'),
    walk('orrin', 'cg_steps', 70, True),
    {'do': 'remove', 'actor': 'orrin'},
    *end_control(),
]})
# ===== end of ACT II, part 2 =====


story = {
    '_about': "The prologue and Act I, scene by scene, from 'Screenplay.md'. Each scene says what "
              "starts it ('on'), when it may ('when': flags), and what happens ('steps'); see StoryDirector in "
              "src/world/story.h for every step and trigger.",
    'tips': TIPS,
    'actors': ACTORS,
    # Act I: a character who woke in Havenbrook learns the gathering trades from
    # the people who work them (scenes 23-26), and not before Elder Vask wakes.
    'locks': [{'skill': skill, 'when': when(['PROLOGUE'], ['ACT1_06_VASK_AWAKE']),
               'text': "You don't know how yet -- and everyone in Havenbrook who could teach you is asleep."}
              for skill in ('Woodcutting', 'Fishing', 'Mining')] + [
        # Weaving opens with Wynn's loom, when she wakes (43F); brewing when
        # Oona teaches it, once Mossvale is cured (Act II).
        {'skill': 'Clothier', 'when': when(['PROLOGUE'], ['ACT1_HAVENBROOK_LOOM_OPEN']),
         'text': "You don't know how to work a loom yet -- and Havenbrook's clothier is asleep."},
        {'skill': 'Brewing', 'when': when(['PROLOGUE'], ['ACT2_OONA_TEACHES']),
         'text': "You don't know how to brew yet."}],
    'scenes': S,
}
open(OUT, 'w', encoding='utf-8', newline='').write(json.dumps(story, indent=1, ensure_ascii=False) + '\n')
print(len(S), 'scenes')
