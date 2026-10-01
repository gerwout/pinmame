#!/usr/bin/env python3
"""A game's names (src/wpc/pinheck_names.h) against the driver's numbering, and the pinHeck system script for VPX.
  names.py check SRC [GAME]   every name has a number the driver gives that device; GAME.c and pinheck.c agree
  names.py vbs SRC [GAME]     the system script a table loads with LoadVPM (to stdout): pinheck.vbs for dominos,
                              GAME.vbs (rzspook.vbs, ...) for the others, which say so in their first line
GAME is dominos (the default), rzspook or jetsons."""
import re
import sys

TABLES = ('switch', 'lamp', 'solenoid')
TITLE = {'dominos': "Domino's Spectacular Pinball Adventure", 'rzspook': "Rob Zombie's Spookshow International",
         'jetsons': 'The Jetsons'}
# the ids of servo 0 and the external LED's first channel
ROLES = {'dominos': ('sNoid', 'sExtR'), 'rzspook': ('sGate', 'sLDGR'), 'jetsons': ('sOrbitty', 'sExtR')}


def read(src, game='dominos'):
    text = open(src + '/wpc/pinheck_names.h').read()
    out = {}
    for t in TABLES:
        body = text.split('pinheck_%s_%s_names[] = {' % (game, t), 1)[1].split('{ 0 }', 1)[0]
        out[t] = [(int(n), i, name) for n, i, name in re.findall(r'\{\s*(\d+),\s*"(\w+)",\s*"([^"]*)"\s*\}', body)]
    return out


def define(path, name):
    m = re.search(r'#define %s\s+(\d+)' % name, open(path).read())
    return int(m.group(1))


def check(src, game='dominos'):
    names, fails = read(src, game), []
    matrix = [c * 10 + r for c in range(1, 9) for r in range(1, 9)]
    valid = {'switch': set(range(1, 9)) | set(matrix) | set(range(91, 99)) | {112, 114},
             'lamp': set(matrix) | {91},
             'solenoid': set(range(1, 33)) | set(range(37, 45)) | set(range(51, 65))}
    ids, lower = {}, set()
    for t in TABLES:
        nums = [n for n, _, _ in names[t]]
        if len(set(nums)) != len(nums):
            fails.append('%s numbers repeat' % t)
        for n, i, name in names[t]:
            if n not in valid[t]:
                fails.append('%s %d (%s) is no %s the driver has' % (t, n, name, t))
            if i.lower() in lower:
                fails.append('id %s repeats (VBScript ignores case)' % i)
            lower.add(i.lower())
            ids[i] = n
    # the header's "Not listed: switches ... and lamps a-b" must give unnamed numbers the driver has
    sec = open(src + '/wpc/pinheck_names.h').read().split('(pinheck_%s_*)' % game, 1)[1].split('(pinheck_', 1)[0].split('*/', 1)[0]
    m = re.search(r'Not listed: switches ([\d,\sand]+?),?\s+and lamps\s+(\d+)-(\d+)', sec)
    if m:
        unlisted = [('switch', int(n)) for n in re.findall(r'\d+', m.group(1))]
        unlisted += [('lamp', n) for n in range(int(m.group(2)), int(m.group(3)) + 1)]
        for t, n in unlisted:
            if n not in valid[t] or n in [k for k, _, _ in names[t]]:
                fails.append('the comment calls %s %d unlisted; it is %s' % (t, n, 'listed' if n in valid[t] else 'no %s the driver has' % t))
    sim = dict((i, int(n)) for i, n in re.findall(r'#define (s\w+)\s+(\d+)', open(src + '/wpc/sims/pinheck/%s.c' % game).read()))
    for i, n in sim.items():
        if ids.get(i) != n:
            fails.append('%s.c has %s = %d, pinheck_names.h %s' % (game, i, n, ids.get(i)))
    drv = src + '/wpc/pinheck.c'
    lamp = define(drv, 'PINHECK_LAMP_ST')
    want = {'sGI0': define(drv, 'PINHECK_SOL_GI0') + 1, 'sGI8': 37, 'sRGB1R': define(drv, 'PINHECK_SOL_RGB') + 1,
            ROLES[game][0]: define(drv, 'PINHECK_SOL_SRV') + 1, ROLES[game][1]: define(drv, 'PINHECK_SOL_EXT') + 1,
            'lStart': (lamp // 8 + 1) * 10 + lamp % 8 + 1, 'swLFlip': define(src + '/wpc/pinheck.h', 'PINHECK_SWLFLIP'),
            'swRFlip': define(src + '/wpc/pinheck.h', 'PINHECK_SWRFLIP')}
    for i, n in want.items():
        if ids.get(i) != n:
            fails.append('%s is %s, the driver has %d' % (i, ids.get(i), n))
    for f in fails:
        print('NAMES FAIL: ' + f)
    print('names %s: %d switches, %d lamps, %d solenoid outputs; %d sim constants agree' % (
        game, len(names['switch']), len(names['lamp']), len(names['solenoid']), len(sim)))
    return 1 if fails else 0


HANDLERS = r'''
' Keyboard handlers: flippers through PinMAME's flipper column; Enter 0, Back 7, User 9 as in PinMAME
Function vpmKeyDown(ByVal keycode)
	vpmKeyDown = True
	With Controller
		Select Case keycode
			Case LeftFlipperKey  .Switch(swLLFlip) = True : vpmKeyDown = False
			Case RightFlipperKey .Switch(swLRFlip) = True : vpmKeyDown = False
			Case keyInsertCoin1  vpmTimer.PulseSw swCoin
			Case keyInsertCoin2  vpmTimer.PulseSw swCoin
			Case keyInsertCoin3  vpmTimer.PulseSw swCoin
			Case StartGameKey    .Switch(swStart) = True
			Case keyEnter        .Switch(swEnter) = True
			Case keyCancel       .Switch(swBack) = True
			Case keyUp           .Switch(swUser) = True
			Case keyCoinDoor     .Switch(swCoinDoor) = Not .Switch(swCoinDoor)
			Case keyBangBack     vpmNudge.DoMechTilt
			Case keyVPMVolume    vpmVol
			Case Else            vpmKeyDown = False
		End Select
	End With
End Function

Function vpmKeyUp(ByVal keycode)
	vpmKeyUp = True
	With Controller
		Select Case keycode
			Case LeftFlipperKey  .Switch(swLLFlip) = False : vpmKeyUp = False
			Case RightFlipperKey .Switch(swLRFlip) = False : vpmKeyUp = False
			Case StartGameKey    .Switch(swStart) = False
			Case keyEnter        .Switch(swEnter) = False
			Case keyCancel       .Switch(swBack) = False
			Case keyUp           .Switch(swUser) = False
			Case keyShowOpts     .Pause = True : vpmShowOptions : .Pause = False
			Case keyShowKeys     .Pause = True : vpmShowHelp : .Pause = False
			Case keyReset        .Stop : BeginModal : .Run : vpmTimer.Reset : EndModal
			Case Else            vpmKeyUp = False
		End Select
	End With
End Function
'''

HEAD = r'''
' pinHeck (Spooky Pinball) system script: %s. Generated by
' tests/pinheck/vpx/names.py from src/wpc/pinheck_names.h; do not edit.
Option Explicit
LoadCore
Private Sub LoadCore
	On Error Resume Next
	If VPBuildVersion < 0 Or Err Then
		Dim fso : Set fso = CreateObject("Scripting.FileSystemObject") : Err.Clear
		ExecuteGlobal fso.OpenTextFile("core.vbs", 1).ReadAll    : If Err Then MsgBox "Can't open ""core.vbs""" : Exit Sub
		ExecuteGlobal fso.OpenTextFile("VPMKeys.vbs", 1).ReadAll : If Err Then MsgBox "Can't open ""vpmkeys.vbs""" : Exit Sub
	Else
		ExecuteGlobal GetTextFile("core.vbs")    : If Err Then MsgBox "Can't open ""core.vbs"""    : Exit Sub
		ExecuteGlobal GetTextFile("VPMKeys.vbs") : If Err Then MsgBox "Can't open ""vpmkeys.vbs""" : Exit Sub
	End If
End Sub

vpmSystemHelp = "pinHeck keys:" & vbNewLine &_
  vpmKeyName(keyEnter)       & vbTab & "Enter (menu)" & vbNewLine &_
  vpmKeyName(keyCancel)      & vbTab & "Back"         & vbNewLine &_
  vpmKeyName(keyUp)          & vbTab & "User"         & vbNewLine &_
  vpmKeyName(keyCoinDoor)    & vbTab & "Coin Door"
'''


def vbs(src, game='dominos'):
    names = read(src, game)
    out = [HEAD.lstrip('\n') % TITLE[game]]
    if game != 'dominos':
        out.insert(0, "' %s.vbs: save under this name (pinheck.vbs is Domino's); its table loads it with LoadVPM\n" % game)
    for t in TABLES:
        out.append("\n' %s\n" % {'switch': 'Switches', 'lamp': 'Lamps', 'solenoid': 'Solenoid outputs'}[t])
        out += ['Const %-20s = %3d  \' %s\n' % (i, n, name) for n, i, name in names[t]]
    out.append(HANDLERS)
    sys.stdout.write(''.join(out))
    return 0


if __name__ == '__main__':
    if len(sys.argv) not in (3, 4) or sys.argv[1] not in ('check', 'vbs') or sys.argv[3:] and sys.argv[3] not in TITLE:
        sys.exit(__doc__)
    sys.exit((check if sys.argv[1] == 'check' else vbs)(*sys.argv[2:]))
