# Thumbpad Keyboard Configuration Language v3.4

## 1. Introduction

This document describes the configuration file format (v3.4) used to define keyboard layouts and actions for the ESP32 Thumbpad device. This format allows for defining button appearance, grid layout (explicitly or automatically with optional sizing), and complex HID keyboard actions including **sequential character typing (`"string"`)**, **simultaneous key presses (`'keys'`)**, default and explicit delays using `(<ms>)` syntax, toggles, modifiers with defined persistence, explicit modifier release (`\MOD`), and explicit key release control (`|`).

**Key changes in v3.4:**
*   The **GridInfo** specifier is now **optional** and can be **0, 2, or 4 digits**:
    *   **0 digits (omitted)**: Auto-place a **1x1** button.
    *   **2 digits (`ColSpan RowSpan`)**: Auto-place a button with the specified **span**.
    *   **4 digits (`Col ColSpan Row RowSpan`)**: Explicit placement and span.
*   The Toggle specifier (`T`) remains at the beginning of the line.
*   Default delay, modifier persistence, `\MOD` release, `'keys'` simultaneous press, and `"string"` typing behavior remain as defined in v3.2.

## 2. File Format Overview

Configuration files are plain text files (.cfg).

*   First Line (Mandatory): Grid Definition
    *   **Grid Definition**: `<Cols>x<Rows>[ (<DefaultDelayMS>)]`
        *   `<Cols>`: Number of columns in the grid (e.g., 5).
        *   `<Rows>`: Number of rows in the grid (e.g., 4).
        *   `(<DefaultDelayMS>)` (Optional): Overrides the global default delay (50ms) for sequential actions within this file. Example: `5x4 (20)` sets the default delay to 20ms.
*   **Button Definitions**: Button Definitions or Comments
    *   Button Definition: `[T][<GridInfo>]<Label>\t<ActionString>`
        *   `T` (Optional): If present as the *first character* on the line, changes the button interaction to Toggle mode (see Section 4.2).
        *   `<GridInfo>` (Optional): 0, 2, or 4 digits specifying placement and/or span. Must appear immediately after `T` if `T` is present.
            *   **Omitted (0 digits)**: Button defaults to 1x1 span and is auto-placed.
            *   **2 digits (`ColSpan RowSpan`)**: Button has the specified span (e.g., `21` for 2x1) and is auto-placed. Both digits must be >= 1.
            *   **4 digits (`Col ColSpan Row RowSpan`)**: Button is explicitly placed at `Col`, `Row` with the specified span. All span digits must be >= 1.
        *   `<Label>`: Text displayed on the button (UTF-8 allowed). Starts immediately after `T` (if present) and `GridInfo` (if present).
        *   `\t`: A literal tab character separates the Label from the ActionString.
        *   `<ActionString>`: Defines the HID action (see Section 4).
*   **Comment**: Lines starting with `#` are ignored.
*   **Empty Lines**: Ignored.

## 3. Grid, Label, and Placement

*   **Grid**: Defines the overall button layout dimensions using `Cols` and `Rows`.
*   **Label**: The text shown on the button. Keep labels concise for readability.
*   **Placement**:
    *   **Explicit Placement (4-digit GridInfo)**: If `<GridInfo>` (`Col ColSpan Row RowSpan`) is provided, the button is placed at the specified `Col`, `Row` and occupies `ColSpan` x `RowSpan` cells. Overlapping buttons defined explicitly result in an error.
    *   **Automatic Placement (0 or 2-digit GridInfo)**:
        *   **Span**: If `<GridInfo>` is omitted (0 digits), the span defaults to 1x1. If `<GridInfo>` is 2 digits (`ColSpan RowSpan`), the button uses that span.
        *   **Position**: The system places the button's top-left corner in the first available cell (searching row-major: left-to-right, then top-to-bottom) where the button's entire `ColSpan` x `RowSpan` area fits without overlapping any previously placed buttons (either explicit or auto-placed).
        *   If no suitable slot is found for the required span, it results in an error.

## 4. Action String Syntax

The `<ActionString>` defines what happens when a button is interacted with. It's composed of sequential *Action Components*. Spaces separate sequential components, implying the **default delay** between them unless an explicit `(<ms>)` component is used.

*   **Overall Structure**: `<Press Sequence>[|[<ms>] <Release Sequence>]`
*   **Action Components**: The building blocks of sequences. Can be:
    *   Simultaneous Key Press Literal (`'keys'`)
    *   Special Key Name (`{KEY}`)
    *   Typing Sequence (`"string"`)
    *   Modifier (`LC`, `LS`, etc.) - *Acts as a prefix and persists*
    *   Explicit Modifier Release (`\LC`, `\LS`, etc.)
    *   Delay (`(<ms>)`)
    *   Layout Change (`G<file>`)
    *   Release Control (`|` prefix) - *Only at the very start*

### 4.1. Action Components Defined

*   **Simultaneous Key Press Literal (`'`)**:
    *   Syntax: One or more printable ASCII characters enclosed in single quotes (`'keys'`).
    *   Action: Represents **simultaneously pressing** the single, base HID keycodes for *all* characters within the quotes (US QWERTY assumed). **Does NOT perform an implicit release.**
    *   Purpose: Used for pressing multiple specific keys together, especially for shortcuts or holding keys.
    *   Case is ignored for keycode mapping (`'q'` and `'Q'` both press keycode `0x14`). To send an uppercase 'Q', use `LS'q'` or `LS'Q'`.
    *   Example: `'q'` (Press key 0x14), `' '` (Press Space 0x2C), `''` (Press single quote 0x34), `'qwe'` (Press Q, W, E simultaneously).
    *   Limited to 6 non-modifier keys per simultaneous group.
*   **Special Key Name (`{}`)**:
    *   Syntax: Common non-printable key names or specific functions enclosed in curly braces `{}`.
    *   Action: Represents **pressing** the single HID keycode or invoking the function for that special key. **Does NOT perform an implicit release.**
    *   Purpose: Pressing non-character keys (Enter, F1, Del, etc.).
    *   Example: `{DEL}` (Press Delete 0x4C), `{ENTER}` (Press Enter 0x28).
    *   **Simultaneous Press**: Can be combined with other Special Key Names or placed inside a Simultaneous Key Press Literal *without spaces*. Example: `'c'{DEL}` (Press C and Delete simultaneously), `{CTRL}{ALT}{DEL}` (Press Ctrl, Alt, Delete simultaneously - *Note: Prefer modifier codes `LC LA {DEL}` for clarity*).
    *   See Appendix A for supported names.
*   **Typing Sequence (`"`)**:
    *   Syntax: One or more characters enclosed in double quotes (`"`).
    *   Action: Represents a sequence of **press-and-release** actions for each character, simulating typing.
    *   **Purpose**: Easily sending strings of text or complex multi-key shortcuts.
    *   **Behavior**:
        *   Each character is processed sequentially: Press Key(s), Release Key(s), **Default Delay**.
        *   **Case Sensitive**: Automatically handles the Shift modifier. `"H"` sends Shift+H (press/release), `"h"` sends H (press/release).
        *   Spaces inside quotes are typed as spaces (`"Hello World"`).
        *   Special Key Names can be embedded: `"cat file.txt{ENTER}"` (Types 'c', 'a', 't', ' ', 'f', ..., 't', then presses+releases Enter). Uses default delay between each.
        *   **Modifier Interaction**: If prefixed by modifier codes (e.g., `LC"kd"`), the modifier(s) are **held down** during the entire typing sequence and are **released automatically after** the sequence completes, unless modifier persistence rules (see 4.4) dictate they remain held for subsequent actions.
    *   Example: `"Hello"` (Types 'H', delay, 'e', delay, 'l', delay, 'l', delay, 'o').
    *   Example: `LC"kd"` (Press LC, (Press K, Release K, delay), (Press D, Release D, delay), Release LC. Simulates Ctrl+K, D shortcut).
*   **Modifier Codes**:
    *   Codes: `LC`, `LS`, `LA`, `LG`/`LM`, `RC`, `RS`, `RA`, `RG`/`RM`.
    *   Usage: Placed immediately before a Key Press Literal (`'keys'`), Special Key Name (`{KEY}`), or a Typing Sequence (`"string"`) component they apply to within the `<Press Sequence>`.
    *   **Persistence**: See Section 4.4.
*   **Explicit Modifier Release (`\MOD`)**:
    *   Syntax: A backslash (`\`) followed by a Modifier Code (e.g., `\LC`, `\LS`).
    *   Action: Releases the specified modifier(s).
    *   Purpose: Allows releasing a modifier mid-sequence while other keys might still be pressed or before subsequent actions that shouldn't have the modifier.
    *   Example: `LS'a' \LS 'b'` (Press Shift+A, release Shift, press B).
*   **Explicit Delay (`(<ms>)`)**:
    *   Syntax: Milliseconds enclosed in parentheses (e.g., `(100)`, `(5)`).
    *   Action: Introduces a pause *after* the preceding action component completes and *before* the next sequential action component starts. **Overrides the default delay.**
    *   Usage: Placed as a separate component in the sequence, separated by spaces.
    *   Example: `'A' (500) "BC"` (Press A, wait 500ms, Type B, delay, Type C).
*   **Layout Change (`G<file>`)**:
    *   Syntax: `G` followed by the filename (e.g., `Gsymbols.cfg`).
    *   Action: Loads the specified layout file after completing any preceding actions in the current sequence component.
    *   Example: `"logout"{ENTER} (1000) Glogin.cfg`

### 4.2. Action Types & Interaction Modes

*   **Momentary (Default - No `T` prefix)**:
    *   Touch Press: Initiates the `<Press Sequence>`.
    *   Touch Release:
        *   If no `|`: Sends release for all keys/modifiers activated *and left held* at the end of the `<Press Sequence>`. (Note: Keys/modifiers from `"` typing sequences are usually already released unless they persist).
        *   If `|`: Triggers the execution of the `<Release Sequence>`.
*   **Toggle (`T` prefix at start of line)**:
    *   Format: `T[<GridInfo>]<Label>\t<ActionString>`
    *   **First Click (Press & Release)**:
        *   Executes the **`<Press Sequence>`**.
        *   Button visually highlights. System tracks keys/modifiers left active (held) at the end of the sequence.
    *   **Second Click (Press & Release)**:
        *   If `|`: Executes the `<Release Sequence>`.
        *   If no `|`: Releases each key/modifier that was left active by the `<Press Sequence>`.
        *   Button highlight is removed.
    *   Subsequent clicks alternate.

### 4.3. Sequences and Timing

*   **Sequential Actions**: Separate Action Components (`'keys'`, `{KEY}`, `"string"`, `(<ms>)`, `G<file>`, `\MOD`) with one or more spaces. They execute in order.
    *   **Default Delay**: Applies between space-separated components unless an explicit `(<ms>)` component is used. Defined by `(<ms>)` in grid definition or global default. Also applies between characters/keys within a `"string"`.
    *   **Explicit Delays**: Use the `(<ms>)` component to introduce specific pauses, overriding the default delay for that step.
    *   Example: `'A' 'B' 'C'` (Press A, default delay, Press B, default delay, Press C).
    *   Example: `'A' (100) 'B' (20) 'C'` (Press A, wait 100ms, Press B, wait 20ms, Press C).
    *   Example: `"AB" (50) 'c'` (Type A, delay, Type B, wait 50ms, Press C).
*   **Simultaneous Press**: Use the multi-character single quote literal (`'keys'`) or concatenate Special Key Names (`{KEY}{KEY}`) without spaces. Modifiers prefix the group.
    *   Example: `LC'qwe'` (Press Left Ctrl + Q + W + E together).
    *   Example: `{CTRL}{ALT}{DEL}` (Press Ctrl, Alt, Delete together).
    *   **Note**: You cannot make `"string"` components simultaneous with others. `"AB"` is typing 'A' then 'B'. `'AB'` is pressing A and B together.

### 4.4. Modifiers and Persistence

*   **Application**: Modifier codes (`LC`, `LS`, etc.) apply to the *immediately following* action component (`'keys'`, `{KEY}`, `"string"`).
*   **Persistence Rule**: When a modifier is activated by prefixing a component, it **remains active (pressed)** for any subsequent *sequential* components within the *same* `<Press Sequence>` **unless**:
    1.  It is explicitly released using `\MOD` (e.g., `\LC`).
    2.  Another modifier code for the *same* modifier key (e.g., `RC` overriding `LC`, or `LC` reapplying `LC`) is encountered, resetting its state for the new component.
    3.  The `<Press Sequence>` ends.
*   **Release**: Modifiers still active at the end of the `<Press Sequence>` are released:
    *   Implicitly on touch release (Momentary mode, no `|`).
    *   Implicitly on the second click (Toggle mode, no `|`).
    *   Explicitly via the `<Release Sequence>` if `|` is used.
    *   Explicitly via `\MOD` components.
*   **Typing Sequence (`"string"`) Exception**: As noted in 4.1, modifiers prefixed to a `"string"` are typically released *after* the string completes, unless followed immediately by another component that requires the same modifier (implicitly or explicitly).
*   **Examples**:
    *   `LS'a'` (Press LS, Press a. LS and a released on touch release).
    *   `LS'a' 'b'` (Press LS, Press a, delay, Press b [LS still held]. LS, a, b released on touch release).
    *   `LS'a' \LS 'b'` (Press LS, Press a, delay, Release LS, delay, Press b. Only b released on touch release).
    *   `LC'a' LS'b'` (Press LC, Press a, delay, Press LS, Press b [LC still held]. LC, LS, a, b released on touch release).
    *   `LC"kd"` (Press LC, Type K, delay, Type D, delay, Release LC).
    *   `LC"k" 'd'` (Press LC, Type K, delay, Release LC, delay, Press D. Only D released on touch release). *This shows the "typing sequence modifier release" behavior.*
    *   `LC'k' 'd'` (Press LC, Press K, delay, Press D [LC still held]. LC, K, D released on touch release).

### 4.5. Explicit Release Sequence (`|` separator)

*   Format: `<Press Sequence> |[(<ms>)] <Release Sequence>`
*   `|`: Separates press actions from release actions.
*   `[(<ms>)]` (Optional): Specifies a delay *after* the release trigger (touch release or toggle-off click) and *before* the first action in the `<Release Sequence>`. If omitted, the **default delay** is used. A delay component `(<ms>)` just before the `|` is simply the last part of the `<Press Sequence>`.
*   `<Release Sequence>`: Specifies which keys and/or modifiers to release. Follows the same syntax rules as `<Press Sequence>` (using `'keys'`, `{KEY}`, `(<ms>)`, modifier codes for *release*, `\MOD` is not typically needed here but possible) but the actions are *releases*. `"string"` components are generally not useful here.
*   **Important**: If modifiers were held during the `<Press Sequence>`, they must be explicitly listed in the `<Release Sequence>` if you want them released by this action.
*   Triggering: Unchanged (Momentary: on release; Toggle: on second click).
*   Example: `LS'c' |(0) 'c' LS` (Press Shift+C. On release: no delay, Release C, default delay, Release Shift).
*   Example: `'A' (200) | 'A'` (Press A, wait 200ms minimum. On release: default delay, Release A).
*   Example: `LC'k' 'd' | 'd' 'k' LC` (Press LC+K, delay, Press D [LC held]. On release: default delay, Release D, default delay, Release K, default delay, Release LC).

### 4.6. Release Control Prefix (`|` at start)

*   Format: `|<Release Modifiers><ActionString>` or `|X<ActionString>`
*   Release Specific Modifiers: `|LSLG Gmenu.cfg` (Release LS & LG, then go to menu.cfg).
*   Release All Keys (`|X`):
    *   Sends empty HID report (all keys up).
    *   Resets toggle state of all buttons *in the currently loaded layout* to OFF.
    *   Sends modifier update `0x00` via ESP-NOW *if paired*.
    *   Example: `|X Gmain.cfg` (Release all, reset toggles, clear remote mods, go to main).

## 5. Execution Flow Summary

1.  **Parse**: Check syntax line by line.
    *   Check for leading `T`.
    *   Check if next characters are digits (GridInfo).
        *   If 4 digits: Store explicit placement (`Col`, `Row`) and span (`ColSpan`, `RowSpan`). Mark as explicitly placed.
        *   If 2 digits: Store span (`ColSpan`, `RowSpan`). Mark for auto-placement with specified span.
        *   If 0 digits (no digits follow `T` or at start): Default span to 1x1. Mark for auto-placement with 1x1 span.
    *   Extract Label up to `\t`.
    *   Parse ActionString.
    *   Store default delay from grid definition.
2.  **Layout Calculation**: After parsing all buttons, resolve auto-placements. Iterate through the grid cells (row-major). For each auto-placed button (in definition order), find the first empty cell (`r`, `c`) where the button's `ColSpan` x `RowSpan` rectangle fits within the grid boundaries and doesn't overlap any previously placed buttons (explicit or auto). Assign (`r`, `c`) as the button's top-left position. Check for explicit placement overlaps. Report errors if overlaps occur or if auto-placement fails.
3.  **Runtime Execution**:
    *   **Touch Press**:
        *   Handle `|` prefix (`|Modifiers`, `|X`) if present.
        *   If Momentary: Execute `<Press Sequence>` (respecting default/explicit delays and modifier persistence).
        *   If Toggle: Do nothing (wait for release/click).
    *   **Touch Release**:
        *   If Momentary:
            *   No `|`: Release keys/modifiers left held at the end of `<Press Sequence>`.
            *   Has `|`: Execute `<Release Sequence>` (after optional `|(<ms>)`, respecting delays).
        *   If Toggle (`T`):
            *   If OFF: Execute `<Press Sequence>`, set state ON (highlight), track held keys/modifiers at sequence end.
            *   If ON:
                *   No `|`: Release keys/modifiers left held by `<Press Sequence>`.
                *   Has `|`: Execute `<Release Sequence>` (after optional `|(<ms>)`).
                *   Set state OFF (unhighlight).
    *   **Layout Change (`G<file>`)**: Triggered after preceding actions complete.

## 6. Examples (v3.4 Syntax)

*Assume a 5x4 grid and default delay of 50ms unless specified otherwise.*

*   **Explicit Placement (4 digits)**:
    *   `0101A\t'a'` (Momentary, Col 0, Row 0, 1x1, Label "A", Action 'a')
    *   `T1211Ctrl\tLC` (Toggle, Col 1, Row 1, 2x1, Label "Ctrl", Action LC)
*   **Auto Placement - Default Span (0 digits)**:
    *   `Q\t'q'` (Momentary, 1x1, placed in first free slot, Label "Q", Action 'q')
    *   `TW\t'w'` (Toggle, 1x1, placed in next free slot, Label "W", Action 'w')
    *   `E\t'e'`
    *   `R\t'r'`
    *   `T\t't'`
    *   `Y\t'y'`
    *   *(... and so on, filling the grid row by row)*
*   **Auto Placement - Specified Span (2 digits: `ColSpan RowSpan`)**:
    *   `21Enter\t{ENTER}` (Auto-placed 2x1 button, Label "Enter")
    *   `12Shift\tLS` (Auto-placed 1x2 button, Label "Shift")
    *   `T22BigToggle\t"Toggle Me"` (Toggle, Auto-placed 2x2 button)
*   **Mixed Placement Example**:
    ```
    5x4 (20) # 5 columns, 4 rows, 20ms default delay
    # Explicit placements first
    0100A\t'a'
    1100B\t'b'
    4100C\t'c'
    0110D\t'd'
    # Auto placements fill the gaps
    E\t'e'      # Auto 1x1 -> likely at 2,0
    F\t'f'      # Auto 1x1 -> likely at 3,0
    21G HI\t"gh" # Auto 2x1 -> likely at 1,1 (spanning 1,1 and 2,1)
    J\t'j'      # Auto 1x1 -> likely at 3,1
    K\t'k'      # Auto 1x1 -> likely at 4,1
    22Big\t' '   # Auto 2x2 -> likely at 0,2 (spanning 0,2 1,2 0,3 1,3)
    L\t'l'      # Auto 1x1 -> likely at 2,2
    ```
*   **Simultaneous Press**:
    *   `0101Cut\tLC'x'` (Explicit placement)
    *   `Copy\tLC'c'` (Auto placement 1x1)
    *   `Paste\tLC'v'` (Auto placement 1x1)
*   **Sequential Key Presses**:
    *   `Seq\t'q' (10) 'w' (100) 'e'` (Auto placement 1x1)
    *   `SeqDef\t'q' 'w' 'e'` (Auto placement 1x1, uses default delay)
*   **Typing Strings**:
    *   `Greet\t"Hello World!"` (Auto placement 1x1)
    *   `TCode\t"if (x > 0) {NL}{TAB}// TODO{NL}"` (Toggle, Auto placement 1x1)
*   **Modifiers & Persistence**:
    *   `Shift1\tLS'1'` (Auto placement 1x1, sends '!')
    *   `CapsHello\tLS"hello"` (Auto placement 1x1, sends 'HELLO')
    *   `CtrlK D\tLC"kd"` (Auto placement 1x1, sends Ctrl+K, D sequence)
    *   `CtrlA B\tLC'a' 'b'` (Auto placement 1x1, sends Ctrl+A, then Ctrl+B)
    *   `CtrlA then B\tLC'a' \LC 'b'` (Auto placement 1x1, sends Ctrl+A, then just B)
*   **Explicit Release**:
    *   `HoldA\t'A' (500) | 'A'` (Auto placement 1x1)
    *   `T HoldA\tT'A' (500) |(20) 'A'` (Toggle, Auto placement 1x1)
*   **Release Control & Layout Change**:
    *   `Panic\t|X Gmain.cfg` (Auto placement 1x1)
    *   `Done\t"Done." (500) Gmain.cfg` (Auto placement 1x1)


## Appendix A: Special Key Names (`{NAME}`) and Modifier Codes (Prefixes)

**(Appendix content remains the same as v3.2 - listing Modifier Codes and Special Key Names with Hex codes)**

**Modifier Codes (Prefixes & Release):**

| Code | Modifier       | Release Code | Notes                                   |
| :--- | :------------- | :----------- | :-------------------------------------- |
| LC   | Left Control   | \LC          | Prefix applies, persists. `\` releases. |
| LS   | Left Shift     | \LS          | Prefix applies, persists. `\` releases. |
| LA   | Left Alt       | \LA          | Prefix applies, persists. `\` releases. |
| LG   | Left GUI       | \LG          | Prefix applies, persists. `\` releases. |
| LM   | Left Meta      | \LM          | (Alias for LG)                          |
| RC   | Right Control  | \RC          | Prefix applies, persists. `\` releases. |
| RS   | Right Shift    | \RS          | Prefix applies, persists. `\` releases. |
| RA   | Right Alt      | \RA          | Prefix applies, persists. `\` releases. |
| RG   | Right GUI      | \RG          | Prefix applies, persists. `\` releases. |
| RM   | Right Meta     | \RM          | (Alias for RG)                          |

**Special Key Names (`{NAME}`):** Represent pressing a specific non-character key.

| Name      | Hex  | Notes                   |
| :-------- | :--- | :---------------------- |
| {ESC}     | 29   | Escape                  |
| {F1}      | 3A   | Function Key 1          |
| {F2}      | 3B   | Function Key 2          |
| {F3}      | 3C   | Function Key 3          |
| {F4}      | 3D   | Function Key 4          |
| {F5}      | 3E   | Function Key 5          |
| {F6}      | 3F   | Function Key 6          |
| {F7}      | 40   | Function Key 7          |
| {F8}      | 41   | Function Key 8          |
| {F9}      | 42   | Function Key 9          |
| {F10}     | 43   | Function Key 10         |
| {F11}     | 44   | Function Key 11         |
| {F12}     | 45   | Function Key 12         |
| {PRTSC}   | 46   | Print Screen            |
| {SCROLL}  | 47   | Scroll Lock             |
| {PAUSE}   | 48   | Pause/Break             |
| {INS}     | 49   | Insert                  |
| {HOME}    | 4A   | Home                    |
| {PGUP}    | 4B   | Page Up                 |
| {DEL}     | 4C   | Delete Forward          |
| {END}     | 4D   | End                     |
| {PGDN}    | 4E   | Page Down               |
| {RIGHT}   | 4F   | Right Arrow             |
| {LEFT}    | 50   | Left Arrow              |
| {DOWN}    | 51   | Down Arrow              |
| {UP}      | 52   | Up Arrow                |
| {NUMLK}   | 53   | Num Lock                |
| {KP/}     | 54   | Keypad Divide           |
| {KP*}     | 55   | Keypad Multiply         |
| {KP-}     | 56   | Keypad Subtract         |
| {KP+}     | 57   | Keypad Add              |
| {KPENT}   | 58   | Keypad Enter            |
| {KP1}     | 59   | Keypad 1 / End          |
| {KP2}     | 5A   | Keypad 2 / Down         |
| {KP3}     | 5B   | Keypad 3 / PageDn       |
| {KP4}     | 5C   | Keypad 4 / Left         |
| {KP5}     | 5D   | Keypad 5                |
| {KP6}     | 5E   | Keypad 6 / Right        |
| {KP7}     | 5F   | Keypad 7 / Home         |
| {KP8}     | 60   | Keypad 8 / Up           |
| {KP9}     | 61   | Keypad 9 / PageUp       |
| {KP0}     | 62   | Keypad 0 / Insert       |
| {KP.}     | 63   | Keypad . / Delete       |
| {APP}     | 65   | Application (Menu key)  |
| {POWER}   | 66   | Power (System)          |
| {MUTE}    | 7F   | Mute                    |
| {VOLUP}   | 80   | Volume Up               |
| {VOLDN}   | 81   | Volume Down             |
| {ENTER}   | 28   | Enter / Return          |
| {TAB}     | 2B   | Tab                     |
| {SPACE}   | 2C   | Spacebar                |
| {BKSP}    | 2A   | Backspace               |
| {CAPS}    | 39   | Caps Lock               |
| {LCTRL}   | E0   | Left Control (as key)   |
| {LSHIFT}  | E1   | Left Shift (as key)     |
| {LALT}    | E2   | Left Alt (as key)       |
| {LGUI}    | E3   | Left GUI (as key)       |
| {RCTRL}   | E4   | Right Control (as key)  |
| {RSHIFT}  | E5   | Right Shift (as key)    |
| {RALT}    | E6   | Right Alt (as key)      |
| {RGUI}    | E7   | Right GUI (as key)      |

*Note: Modifier codes (LC, LS etc.) act as prefixes affecting subsequent actions and persist. Special key names like {LCTRL}, {LSHIFT} represent pressing *that specific modifier key itself*, which might be needed in rare cases or for remapping.*
