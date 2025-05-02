# Thumbpad Keyboard Configuration Language v2.1

## 1. Introduction

This document describes the configuration file format used to define keyboard layouts and actions for the ESP32 Thumbpad device. This format allows for defining button appearance, grid layout, and complex HID keyboard actions including sequences, delays, toggles, modifiers, and explicit release control.

## 2. File Format Overview

Configuration files are plain text files (.cfg).

* First Line (Mandatory): Grid Definition
    * **Grid Definition**: `<Cols>x<Rows>[ d<DefaultDelayMS>]`
        * `<Cols>`: Number of columns in the grid (e.g., 5).
        * `<Rows>`: Number of rows in the grid (e.g., 4).
        * `d<DefaultDelayMS>` (Optional): Overrides the global default delay (50ms)   for sequential actions within this file. Example: `5x4 d20` sets the default delay to 20ms for this layout.
* **Button Definitions**: Button Definitions or Comments
    * Button Definition: `<GridInfo><Label>\t[T]<ActionString>`
        * `<GridInfo>`: 4 digits specifying position and span: Col ColSpan Row RowSpan (e.g., 0111 = Col 0, Span 1; Row 1, Span 1). Spans must be at least 1.
        * `<Label>`: Text displayed on the button (UTF-8 allowed).
        * `\t`: A literal tab character separates the Label from the ActionString.
        * *`T`* (Optional): If present immediately after the tab, changes the button interaction to Toggle mode (see Section 4.2).
        * `<ActionString>`: Defines the HID action (see Section 4).
* **Comment**: Lines starting with `#` are ignored.
* **Empty Lines**: Ignored.

## 3. Grid and Label

* **Grid**: Defines the button layout using `Cols` and `Rows`. Buttons are placed using 0-based Col/Row indices and span `ColSpan`/`RowSpan` cells.
* **Label**: The text shown on the button. Keep labels concise for readability.

## 4. Action String Syntax

The `<ActionString>` defines what happens when a button is interacted with. It can be composed of one or more components representing keys, modifiers, delays, and control flow.

* **Action String**: `<Press Sequence>[|[d<ms>] <Release Sequence>]`
* Any keys in the `<Press Sequence>` that are not released in the `<Release Sequence>` remain down until another `<Release Sequence>` releases them explicitly.

### 4.1. Key Representations

    Note: it is important to remember that this file represents key presses, not text. For example: 'C will press the C key, which will produce a lower-case c in most places.  To produce a capital C, you have to represent both the shift key and the C key, e.g. LS'C.  Likewise, keypresses jammed together without spaces are sent simultaneously

A single key or a group/sequence of keys can be represented in several ways:

* **Hex Code**: Two hexadecimal digits (00-FF). Represents a standard HID Usage ID.
    * Example: `2C` (Spacebar), `04` ('a'/'A')
    * To avoid ambiguity with delay (d5 = delay 5 milliseconds), Hex codes in the `D0` to `DF` range must start with a capital D.
    * Valid Hex Code regex: `[0-9A-Fabcef][0-9A-Fa-f]`
* **Single Character Literal (`'`)**: A single quote (`'`) followed immediately by one printable ASCII character. Represents the base HID keycode for that character on a standard US QWERTY layout. It does not use or require a closing quote.
    * Case and shift state are ignored for mapping (e.g., `'q` and `'Q` both map to keycode `0x14`).
    * Use explicit Shift modifiers (LS/RS) for shifted characters (e.g., `LS'q` sends capital Q, `LS';` sends colon).
    * Example: `'Q` (keycode 0x14), `' ` (keycode 0x2C), `'1` (keycode 0x1E).
    * To send a literal single quote, use `''` (keycode 0x34).
* **String Literal (`"`)**: One or more printable ASCII characters enclosed in double quotes (`"`). Represents a sequence or group of keycodes.
    * Characters inside are mapped like single character literals (case/shift ignored).
    * If characters are adjacent within the quotes (no spaces), they represent simultaneous key presses. Example: `"QWE"` (Press Q, W, E together).
    * If characters are separated by spaces within the quotes, they represent sequential key presses with the default delay between them. Example: `"Q W E"` (Press Q, delay, Press W, delay, Press E).
    * A single character in quotes is equivalent to the single quote literal. Example: `"Q"` is the same as `'Q`.
* **Special Key Name**: Common non-printable key names enclosed in curly braces {}.
    * Example: `{DEL}` (Delete Forward 0x4C), `{ENTER}` (0x28), `{ESC}` (0x29), `{F1}` (0x3A)
    * See Appendix A for a list of supported names.

### 4.2. Action Types & Interaction Modes

The interaction mode is determined by the optional `T` prefix immediately after the tab character.

* **Momentary (Default - No `T` prefix)**:
    * Touch Press: Initiates the `<Press Sequence>` (see 4.3, 4.4).
    * Touch Release:
        * If no `|` is present: Sends release for all keys/modifiers activated during the `<Press Sequence>`.
        * If `|` is present: Triggers the execution of the `<Release Sequence>` (see 4.5).
* **Toggle (`T` prefix)**:
    * Format: `<GridInfo><Label>\tT<ActionString>`
    * The `T` must be the very first character after the tab.
    * Changes the button's behavior to a click-on / click-off toggle.
    * **First Click (Press & Release)**:
        * Initiates the `<Press Sequence>` (see 4.3, 4.4).
        * The button visually highlights (e.g., `LV_STATE_CHECKED`). The system must track which keys/modifiers were left active by this action.
    * **Second Click (Press & Release)**:
        * If `|` is present, executes the `<Release Sequence>`.
        * If no `|` is present, releases each key pressed in the `<Press Sequence>`.
        * The button highlight is removed.
    * Subsequent clicks alternate between these two states.
* Layout Change (`G` prefix within `<ActionString>`): Loads a new layout file.
    * Format: `G<filename>` (Can be combined with modifiers or sequences before it, or release controls).
    * Example: `Gsymbols.cfg`, `|LC Gedit.cfg`, `|XGmenu.cfg`

### 4.3. Sequences and Timing

Multiple keys can be pressed/released simultaneously or sequentially. The structure is determined by spaces between key representations and within string literals (`"`).

* **Simultaneous**: Concatenate key representations (Hex, `'char`, `{Special}`, or `"string"`) without spaces between them. Keys within a `"string"` literal with no internal spaces are also simultaneous.
    * The keys/modifiers are sent/released in a single HID report.
    * Limited to 6 non-modifier keys per report. Excess keys are ignored (warning logged).
    * Modifiers apply to the entire simultaneous group they precede (see Section 4.4).
    * **Example (Press)**: `04'B{ESC}` (Press a, b, Esc together), `"QWE"` (Press Q, W, E together), `'Q'W'E` (Same as `"QWE"`, press Q+W+E together), `LC"QW"'E` (Press LeftCtrl+Q+W+E simultaneously).
    * **Example (Release)**: `|'E'W'Q` (Release E, W, and Q all at once)
* **Sequential**: Separate key representations (Hex, `'char`, `{Special}`, or `"string"`) with one or more spaces. Spaces within a `"s t r i n g"` literal also create a sequential action within that string.
    * Keys/modifiers are sent/released one after another with a delay between each action.
    * **Default Delay**: Uses the file's default delay (from `WxH d<ms>`) or the global default (50ms). Applies between space-separated items and between items separated by space within a `"s t r i n g"`.
    * **Custom Delay (`d<ms>`)**: Add `d<ms>` suffix immediately after a key/modifier representation (Hex, `'char`, `{Special}`, or `"string"`) to specify the delay after that specific action. The delay occurs before the next sequential item (if any).
    * **Examples (Press, Default Delay)**:
        * `04 'B {ESC}`: A, delay, B, delay, Esc
        * `"A B C"`, A, delay, B, delay, C
        * `'A 'B 'C`: same as previous
        * `"QWE" ' {ENTER}` (Press `QWE` together, delay, press `Space`, delay, press `Enter`).
    * **Example (Press, Custom Delay)**:
        * `'Ad100'Bd20'C`: A, delay 100ms, B, delay 20ms, C
        * `"AB" d80 'C` (Press `A+B` together, wait 80ms, Press `C`).
            * Note that spaces around d80 do not add any additional delay.
    * **Example (Release, Mixed Delay)**:
        * `|'C'd10 'B 'A` (release C, wait 10ms, release B, wait default, release A)
        * `|"WE"d0 'Q` (Release `W+E` together, no delay, Release `Q`)

### 4.4. Modifiers

Standard HID modifiers can be applied as prefixes to keys or sequences within the `<Press Sequence>`.

* Modifier Codes:
    * `LC`: Left Control
    * `LA`: Left Alt
    * `LS`: Left Shift
    * `LG`, `LM`: Left GUI / Meta (Win/Cmd)
    * `RC`: Right Control
    * `RA`: Right Alt
    * `RS`: Right Shift
    * `RG`, `RM`: Right GUI / Meta (Win/Cmd)
* `Usage`: Place modifier codes directly before the key or sequence they apply to in the `<Press Sequence>`. Multiple modifiers can be combined.
* The modifier(s) are pressed before the first key in the simultaneous sequence they are located in, and held until the sequence is released (either by touch release, the end of a Toggle action, or an explicit release sequence).
* Modifiers listed in the `<Release Sequence>` indicate which modifiers to release (see 4.5).
* A modifier that is in the `<Press Sequence>` but not the `<Release Sequence>` continues to be held down until another explicit `<Release Sequence>` releases it.
* Example (Press):
    * `LS'a` (Shift + a -> sends 'A')
    * `LCLA{DEL}` (Left Ctrl + Left Alt + Delete)
    * `LC"QWE"` (Left Ctrl + Q, W, E simultaneously)
    * `RC'A 'B` (Right Ctrl + A, delay, Right Ctrl + B)

### 4.5. Explicit Release Sequence (`|` separator)

Defines a custom sequence for releasing keys/modifiers, used in both Momentary and Toggle modes.

* Format: `<Press Sequence> |[d<ms>] <Release Sequence>`
* The `|` character separates the press actions from the release actions.
* `[d<ms>] (Optional)`: Immediately after `|`, specifies a custom delay (e.g., `|d0` for no delay, `|d100` for 100ms) before the first action in the `<Release Sequence>`. If omitted, the default delay is used. A delay before the `|`, e.g. `d20 |`, is treated as the end of the `<Press Sequence>`.  However, an explicit delay before the `|` implies a `d0` after the `|`.
* `<Release Sequence>`: Specifies which keys and modifiers to release. Follows the same syntax rules as `<Press Sequence>` (simultaneous, sequential, custom `d<ms>` delays). Items listed here are release actions.
* Triggering (Momentary Mode): The `<Release Sequence>` starts executing after the `<Press Sequence>` completes AND the physical touch is released.  If the physical touch is released before the `<Press Sequence>` ends, the `<Release Sequence>` begins immediately after the `<Press Sequence>` ends.
* Triggering (Toggle Mode - `T`): The `<Release Sequence>` starts executing after the `<Press Sequence>` completes AND the second click fires.  Clicking a second time before the `<Press Sequence>` ends has no effect (the duration of the `<Press Sequence>` acts as a debounce timer).
* Example: `'A 'B | 'B 'A` (Press A, delay, Press B. On release: delay, Release B, delay, Release A)
* Example: `LC'Q |d0 LC 'Q` (Press LeftCtrl+Q. On release: no delay, Release LeftCtrl, delay, Release Q)
* Example: `"QWE" | 'E d10 'W 'Q` (Press Q,W,E simultaneously. On release: delay, Release E, wait 10ms, Release W, wait default ms, Release Q)
* Example: `'Q d300 | 'Q` (Hold Q for a minimum of 300ms, or longer if the key is held down)
* Example: `T'Q d300 | 'Q` (Hold Q for a minimum of 300ms, ignoring release toggles until the delay has passed)
* The last two examples are equivalent to `'Q d300` and `T'Q d300`, respectively.

### 4.6. Release Control Prefix (| at start)

Performs a release action before executing the main action string.

* Format: `|<Release Modifiers><ActionString>`
* Release Specific Modifiers: Release listed modifiers before the action.
    * Example: `|LSLG Gmenu.cfg` (Release Left Shift and Left GUI, then go to menu.cfg)
* Release All Keys (`|X`):
    * Sends an empty HID report (all keys up) to the host.
    * Resets the toggle state of all buttons on this device to OFF (and updates visuals).
    * Sends a modifier update message with mask `0x00` via ESP-NOW to the paired device.
    * Example: `|XGmenu.cfg` (Release all host keys, reset all local toggles, clear remote modifiers, then go to menu.cfg)

## 5. Execution Flow Summary

1. Parse: When a layout loads, action strings are parsed. Errors are logged, and the button defaults to no-op. The interaction mode (T or Momentary) is determined.

2. Touch Press:

    * Check for `|` prefix. If present, execute release control (`|Modifiers...` or `|X`).
    * If Momentary Mode:
        * Execute the `<Press Sequence>` (simultaneous or sequential with delays).
    * If Toggle Mode (T):
        * Do nothing on press itself. Wait for release (click event).

3. Touch Release:

    * If Momentary Mode:
        * If no `|` separator: Send release for all keys/modifiers pressed during the `<Press Sequence>`.
        * If `|` separator present: Execute the `<Release Sequence>` (respecting `|d<ms>` and internal delays/grouping).
    * If Toggle Mode (`T`):
        * If button is OFF and no `<Release Sequence>` is in progress, toggle ON:
            * Execute the `<Press Sequence>`.
            * Mark button as toggled ON (visually highlight). Store the set of keys/modifiers left active by this action.
        * If button is ON and no `<Press Sequence>` is in progress, toggle OFF:
            * If no `|` separator: Send release for all keys/modifiers pressed during the `<Press Sequence>`.
            * If `|` separator present: Execute the `<Release Sequence>` (respecting delays).
            * Mark button as toggled OFF (remove highlight). Clear stored keys for this button.

4. Layout Change (`G<file>`): If the action string includes `G<file>`, trigger the layout change process after completing any preceding key actions in the string.

## 6. Examples

* `'Q`, `"Q"`: (Momentary) Press Q on touch, release Q on lift.
* `T'Q`: (Toggle) Click 1: Press Q, highlight. Click 2: Release Q, unhighlight.
* `"QWE"`: (Momentary) Press Q,W,E on touch, release Q,W,E on lift.
* `T"QWE"`: (Toggle) Click 1: Press Q,W,E, highlight. Click 2: Release Q,W,E, unhighlight.
* `'Q 'W 'E`: (Momentary) Press Q, delay, Press W, delay, Press E on touch. Release Q,W,E simultaneously on lift.
* `T'Q 'W 'E`: (Toggle) Click 1: Press Q, delay, W, delay, E. Highlight. Click 2: Release Q,W,E. Unhighlight.
* `'Q'd5 'W'd5000 'E'd100`: (Momentary) Press Q, 5ms, Press W, 5s, Press E on touch. Release Q,W,E on lift.
* `T'Q'd5 'W'd5000 'E'd100`: (Toggle) Click 1: Press Q, 5ms, W, 5s, E. Highlight. Click 2: Release Q,W,E. Unhighlight.
* `'Q 'W 'E | 'E 'W 'Q`: (Momentary) Press Q, delay, W, delay, E on touch. On lift: delay, Release E, delay, Release W, delay, Release Q.
* `T'Q 'W 'E | 'E 'W 'Q`: (Toggle) Click 1 (Press+Release): Press Q, delay, W, delay, E. Highlight. Click 2: delay, Release E, delay, Release W, delay, Release Q. Unhighlight.
* `T LC'A`: (Toggle) Click 1: Press LC, Press A. Highlight. Click 2: Release A, Release LC. Unhighlight.
* `"QWE"|'E'd10'W'Q`: (Momentary) Press Q,W,E simultaneously on touch. On lift: delay, Release E, 10ms, Release W and Q simultaneously.
* `LCLA{DEL}`: (Momentary) Press LC+LA+DEL on touch, release LC+LA+DEL on lift.
* `LC'Q 'D`: (Momentary) Press LC+Q, delay, Press D (LC still held). Release LC, Q, D on lift.
* `LC'Q 'D|'D 'Q LC`: (Momentary) Press LC+Q, delay, Press D (LC still held). On lift: delay, Release D, delay, Release Q, delay, Release LC.
* `T LC'Q 'D|'D 'Q LC`: (Toggle) Click 1 (Press+Release): Press LC+Q, delay, Press D. Highlight. Click 2: delay, Release D, delay, Release Q, delay, Release LC. Unhighlight.
* `|LSLGGmenu.cfg`: Release LS, Release LG, then Go to menu.cfg.
* `|XGmenu.cfg`: Release all host keys, reset local toggles, clear remote mods, then Go to menu.cfg.
* `Gmenu.cfg`: Go to menu.cfg. Any active toggles remain active (state persists).

## Appendix A: Special Key Names (Example US QWERTY Mapping)

| Name | Hex | Code | Notes |
| ---- | --- | ---- | ----- |
| {ESC} | 29 | Escape
| {F1} | 3A | Function Key 1
| {F2} | 3B | Function Key 2
| {F3} | 3C | Function Key 3
| {F4} | 3D | Function Key 4
| {F5} | 3E | Function Key 5
| {F6} | 3F | Function Key 6
| {F7} | 40 | Function Key 7
| {F8} | 41 | Function Key 8
| {F9} | 42 | Function Key 9
| {F10} | 43 | Function Key 10
| {F11} | 44 | Function Key 11
| {F12} | 45 | Function Key 12
| {PRTSC} | 46 | Print Screen
| {SCROLL} | 47 | Scroll Lock
| {PAUSE} | 48 | Pause/Break
| {INS} | 49 | Insert
| {HOME} | 4A | Home
| {PGUP} | 4B | Page Up
| {DEL} | 4C | Delete Forward
| {END} | 4D | End
| {PGDN} | 4E | Page Down
| {RIGHT} | 4F | Right Arrow
| {LEFT} | 50 | Left Arrow
| {DOWN} | 51 | Down Arrow
| {UP} | 52 | Up Arrow
| {NUMLK} | 53 | Num Lock
| {KP/} | 54 | Keypad Divide
| {KP*} | 55 | Keypad Multiply
| {KP-} | 56 | Keypad Subtract
| {KP+} | 57 | Keypad Add
| {KPENT} | 58 | Keypad Enter
| {KP1} | 59 | Keypad 1 / End
| {KP2} | 5A | Keypad 2 / Down
| {KP3} | 5B | Keypad 3 / PageDn
| {KP4} | 5C | Keypad 4 / Left
| {KP5} | 5D | Keypad 5
| {KP6} | 5E | Keypad 6 / Right
| {KP7} | 5F | Keypad 7 / Home
| {KP8} | 60 | Keypad 8 / Up
| {KP9} | 61 | Keypad 9 / PageUp
| {KP0} | 62 | Keypad 0 / Insert
| {KP.} | 63 | Keypad . / Delete
| {APP} | 65 | Application | (Menu key)
| {POWER} | 66 | Power | (System)
| {MUTE} | 7F | Mute
| {VOLUP} | 80 | Volume Up
| {VOLDN} | 81 | Volume Down
| {ENTER} | 28 | Enter / Return
| {TAB} | 2B | Tab
| {SPACE} | 2C | Spacebar
| {BKSP} | 2A | Backspace | (Often same as DEL)
| {CAPS} | 39 | Caps Lock
| {LCTRL} | E0 | Left Control
| {LSHIFT} | E1 | Left Shift
| {LALT} | E2 | Left Alt
| {LGUI} | E3 | Left GUI | (Win/Cmd)
| {LMeta} | E3 | Left Meta | (Alias for LGUI)
| {LM} | E3 | Left Meta | (Alias for LGUI)
| {RCTRL} | E4 | Right Control
| {RSHIFT} | E5 | Right Shift
| {RALT} | E6 | Right Alt
| {RGUI} | E7 | Right GUI | (Win/Cmd)
| {RMeta} | E7 | Right Meta | (Alias for RGUI)
| {RM} | E7 | Right Meta | (Alias for RGUI)
