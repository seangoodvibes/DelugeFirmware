# Setup MIDI sequencing of notes

#OFFICIAL #SYNTH #KIT #CV

```shortcut
press(MIDI), turn(SELECT)
```

Select MIDI channel 1-16 for note output.

# MIDI sequencing of parameters - first select parameter

#OFFICIAL #MIDI

```shortcut
hold(PARAMETER) + turn(SELECT), turn(PARAMETER)
```

Deluge labels do not apply. Use any button to map the function. 'None' means nothing is assigned.

# Record MIDI automation

#OFFICIAL #MIDI

```shortcut
press(RECORD), press(PLAY), turn(PARAMETER)
```

CC with a '.' shown on the LCD indicates automation is already recorded.

# Record MIDI step automation

#OFFICIAL #MIDI

```shortcut
hold(GRID) + turn(PARAMETER)
```

Records MIDI automation per step.

# Change dial control, but keep automation

#OFFICIAL #MIDI

```shortcut
hold(PARAMETER) + hold(SELECT) + turn(SELECT)
```

Parameters with recorded automation are not shown with the normal Change MIDI Parameter command, so this command avoids writing automation in error.

# MIDI note output in a kit clip

#OFFICIAL #KIT

```shortcut
hold(AUDITION) + press(MIDI)
```

Multiple MIDI channels or notes can be set on each row of a kit.

# MIDI note output in a kit clip: MIDI channel assign

#OFFICIAL #KIT

```shortcut
hold(AUDITION) + turn(LOWER_PARAM)
```

# MIDI note output in a kit clip: MIDI note value

#OFFICIAL #KIT

```shortcut
hold(AUDITION) + turn(UPPER_PARAM)
```

# Settings menu for additional MIDI, CV, and Gate parameters

#OFFICIAL #MIDI

```shortcut
hold(SHIFT) + press(SELECT)
```

Contains additional MIDI, CV, and Gate parameters not specified above, including MIDI Thru and PPQN. These settings apply to all songs.

# External controller to play synth or kit

#OFFICIAL #SYNTH #KIT #MIDI #CV

```shortcut
hold(LEARN) + hold(AUDITION) + press(EXTERNAL)
```

In Synth, any audition / row pressed assigns all. In Kit, only the pressed instrument / row is learned. Deluge learns incoming MIDI note data and maps it to the clip.

# External controller to trigger clip (from song mode)

#OFFICIAL #SONG

```shortcut
hold(LEARN) + hold(AUDITION) + press(EXTERNAL)
```

Can trigger clip mutes from Song mode via an external MIDI controller. Deluge uses MIDI notes, not CC values, for mapping.

# External controller to trigger play (from song mode)

#OFFICIAL #SONG

```shortcut
hold(LEARN) + hold(PLAY) + press(EXTERNAL)
```

Can trigger Play from Song mode via an external MIDI controller. Deluge uses MIDI notes, not CC values, for mapping.

# External controller to trigger record (from song mode)

#OFFICIAL #SONG

```shortcut
hold(LEARN) + hold(RECORD) + press(EXTERNAL)
```

Can trigger Record from Song mode via an external MIDI controller. Deluge uses MIDI notes, not CC values, for mapping.

# Unlearn external controller to play synth or kit

#OFFICIAL #SYNTH #KIT #MIDI #CV

```shortcut
hold(SHIFT) + hold(LEARN) + press(AUDITION)
```

Any already learned MIDI functions flash when the Learn/Input button alone is held.

# Unlearn external controller to trigger clip (from song mode)

#OFFICIAL #SONG

```shortcut
hold(SHIFT) + hold(LEARN) + hold(AUDITION)
```

# Unlearn external controller to trigger play (from song mode)

#OFFICIAL #SONG

```shortcut
hold(SHIFT) + hold(LEARN) + hold(PLAY)
```

# Unlearn external controller to trigger record (from song mode)

#OFFICIAL #SONG

```shortcut
hold(SHIFT) + hold(LEARN) + hold(RECORD)
```

# External control of parameter - first select parameter

#OFFICIAL #SYNTH #KIT

```shortcut
hold(LEARN) + press(EXTERNAL)
```

Learns external control of the selected Deluge parameter.

# Unlearn external control of parameter - first select parameter

#OFFICIAL #SYNTH #KIT

```shortcut
hold(SHIFT) + hold(LEARN)
```

# Nudge MIDI clock

#OFFICIAL #MIDI

```shortcut
hold(X) + turn(TEMPO)
```

# Record external notes

#OFFICIAL #SYNTH #KIT #MIDI #CV

```shortcut
press(RECORD), press(PLAY), press(EXTERNAL)
```

Once learned, the clip does not need to be visible to record incoming external notes into it.

# Sync scaling for unusual time signitures

#OFFICIAL #SYNTH #KIT #MIDI #CV

```shortcut
press(SYNC)
```

Press on the loaded clip and it flashes to show sync scaling is enabled. On all other clips, Sync-Scaling is lit but does not flash.

# Mute by external MIDI for individual kit rows - in kit mode

#OFFICIAL #KIT

```shortcut
hold(LEARN) + hold(LAUNCH) + press(EXTERNAL)
```

Learns an external MIDI note to mute individual kit instruments / rows.
