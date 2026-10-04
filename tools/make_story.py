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
    pose('npc_vask_dream', 'attack'),
    say('npc_vask_dream', 'Back, you hollow things! Back!'),
    face('npc_vask_dream', 'player'),
    say('npc_vask_dream', "You've got a face. The rest of them don't. Not anymore. They've been pulling at this town "
                          "for days. I can feel the roots: three out along the edges and one thick one in the square. "
                          "Cut them loose, whoever you are. I'll keep this lot busy."),
    pose('npc_vask_dream', 'attack'),
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
#  Scenes 44-50 -- the Mayor's Dream
# ---------------------------------------------------------------------------------------------
# All three saved: the finale opens (its door asks first: genmaps, Portal "ask").
S.append({'id': 'act1_finale_open', 'on': {'flag': 'ACT1_HALDA_AWAKE'},
          'when': when(['ACT1_BESS_AWAKE', 'ACT1_TANNER_AWAKE'], ['ACT1_FINALE_QUEST_START']), 'steps': [
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
S.append({'id': 'act1_dragon', 'on': {'enter': 'town_havenbrook', 'spawn': 'from_guild_hall'},
          'when': when(['ACT1_FINALE_GIFT_CHOSEN'], ['ACT1_DRAGON_SHADOW_SEEN']), 'steps': [
    bars(True, 0.0),
    cam('porch_cam', zoom=0.9),
    wait(0.6),
    narrate('Havenbrook is awake: townsfolk spill into the streets, smoke rises from the chimneys, and a hammer rings '
            'from the forge.'),
    sfx('anvil', 0.25, 1.1),
    wait(0.8),
    sfx('anvil', 0.25, 1.1),
    {'do': 'fade', 'to': 'black', 'time': 1.4, 'wait': False, 'amount': 0.32},
    wait(1.0),
    # "The townsfolk freeze and look up": everyone in the street, stopped and
    # turned to the sky as the shadow goes over -- all but Vask, who is the
    # scene's own -- and let go again as the light comes back.
    {'do': 'crowd', 'dir': 'up', 'except': ['npc_vask_porch']},
    fx('shadow', **{'from': 'shadow_from', 'to': 'shadow_to'}, time=3.6, alpha=0.6),
    sfx('gust', 1.0),
    wait(3.8),
    fade('clear', 1.2),
    {'do': 'crowd', 'release': True},
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
    {'do': 'tint', 'who': 'npc_vask_porch', 'colour': [255, 255, 255]},
    pose('npc_vask_porch', ''),
    wait(1.0),
    fade('clear', 1.6),
    *end_control(),
]})


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
              for skill in ('Woodcutting', 'Fishing', 'Mining')],
    'scenes': S,
}
open(OUT, 'w', encoding='utf-8', newline='').write(json.dumps(story, indent=1, ensure_ascii=False) + '\n')
print(len(S), 'scenes')
