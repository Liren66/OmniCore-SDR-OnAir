# Controls: keys, mouse buttons and the wheel

**View > Controls (keys and mouse) ...** (also in the command palette, Ctrl+K) lists what the keys, the extra mouse buttons and the mouse
wheel do on the spectrum and the waterfall, and lets you change every one of them. Nothing here has to be touched: the defaults are what
OnAir has always done, and a fresh install behaves as before.

## What the wheel does by default

| Keys held | Over the spectrum | Over the waterfall |
|---|---|---|
| (none) | zoom the frequency **and** the dB range (the max and min of the vertical axis) around the pointer | zoom the frequency (the vertical axis is time: it only zooms while the waterfall is paused) |
| Ctrl | zoom the frequency only | zoom the frequency only |
| Alt | both, as the plain wheel | both |
| Shift, Ctrl + Shift | nothing (Shift+drag selects a signal and the plot leaves the mouse alone) | nothing |

Two other places have their own wheel and are not changed by this page: the colour scale on the waterfall (wheel = widen or narrow the
colour range) and the digits of the frequency box (wheel = step that digit).

If the plain wheel moving the dB range as well bothers you, set **Wheel -> Zoom the frequency only** for the spectrum.

## The wheel table

One row per modifier combination (none, Shift, Ctrl, Alt, Ctrl+Shift), one column for the spectrum and one for the waterfall. Each cell is
one of:

* **Zoom (frequency and level)**: the default plain wheel.
* **Zoom the frequency only** / **Zoom the level / time only** (the dB axis of the spectrum, the time axis of a paused waterfall).
* **Tune by the step**: a notch tunes up or down by the tuning step (below). Several small trackpad movements add up to a step.
* **Pan along the frequency**: slides the view sideways by a tenth of its width a notch.
* **Scroll the waterfall back in time**: pauses the waterfall and scrolls through its rows (resume with the Paused button in the waterfall corner, or the pause key if you bound one).
* **Nothing**.

For example, wheel = zoom the frequency only, Shift + wheel = tune, Ctrl + wheel = pan.

**Tuning step**: "the mode's own step" (the click raster: 100 kHz for FM, 12.5 kHz for the marine band, otherwise the Spectrum menu's snap
step or 1 kHz), or a fixed step from 10 Hz to 1 MHz. **Wheel notches per step** (1 to 5) slows the tuning wheel down.

## Keys and mouse buttons

Each action has one binding: a key (with Ctrl, Shift, Alt as you like) or the middle / back / forward mouse button. Click **Set**, press
the key; **Clear** removes it; **Default** brings the original back. A row turns orange when two actions share a binding (both would
fire); hover it to see which. The left and right buttons, Esc, Ctrl+K and Ctrl+Shift+L stay as they are and cannot be bound.

| Action | Default |
|---|---|
| Start or stop the receiver | Space |
| Mode 1 to 9 of the list | 1 to 9 |
| Spectrum range up / down 5 dB, wider, narrower, default | Ctrl+Up, Ctrl+Down, Ctrl+Shift+Up, Ctrl+Shift+Down, Ctrl+Home (spectrum hovered) |
| View one width up / down in frequency | Ctrl+Page Up, Ctrl+Page Down |
| Tune up / down by the step, zoom in / out, show the whole band, pan left / right, larger / smaller step, pause the waterfall (mouse over the plots), mute | not bound |

A key is ignored while you type in a text field. Modifiers must match exactly: Ctrl+Up does not fire when Shift is held too.

## Where it is saved

In the preferences (`ctlKeys`, `ctlWhSpec`, `ctlWhWf`, `ctlStep`, `ctlNotch`), shared by all receivers, and included in Tools > Backup. Only
what differs from the defaults is written. The web client (OnAir Web) has its own input handling and is not affected.

## For developers

* `app/ui/input_map.h`: the action table, the defaults, the saved text, the conflict check and the wheel rule (pure, tested by
  `tests/app/test_input_map.cpp`); `app/ui/input_map.cpp`: reading the keys and the window.
* `plt::SetNextWheelAxes()` (app/gfx/plot.h) tells the next plot which axes the wheel zooms; `wheelPolicy()` in app/gfx/plots.cpp does the rest
  before the spectrum and waterfall plots. With the default wheel table nothing is called and the plot uses its built-in rule.
