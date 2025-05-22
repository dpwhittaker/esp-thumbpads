# Thumbpad Keyboard Configuration Language v3.12

## 1. Introduction

This document describes the configuration file format (v3.12) used to define keyboard layouts and actions for the ESP32 Thumbpad device. This format allows for defining button appearance (including font size and **icon codes**), grid layout (explicitly or automatically with optional sizing), and complex HID keyboard actions including **sequential character typing (`"string"`)**, **simultaneous key presses (`'keys'`)**, default and explicit delays using `(<ms>)` syntax, toggles, modifiers with defined persistence, explicit modifier release (`\MOD`), and explicit key release control (`|`).


Note: Errors in the file are logged and the system immediately reloads the previous file. Users may be using the thumbpad to author a new file, so every attempt should be made to keep them in a working state. Suggested implementation: rename a working .cfg file to .bkp before replacing it, and rename it back to .cfg if parsing fails.

**Key changes in v3.12:**
*   **Corrected Appendix B (Icon Codes)** to accurately reflect symbols defined by `LV_STR_SYMBOL_...` constants.
*   Escape sequence for a literal dollar sign in label text remains `$$`.
*   Parsing rule for Icon Codes (`$<Name>`) remains longest match.
*   **Clarification**: Parsing of `{MODIFIER_NAME}` (e.g., `{LCTRL}`) results in `ACTION_COMP_TYPE_MOD_PRESS`.
*   **New**: Modifier codes (`LC`, `LS`, etc.) can now be used as standalone action components, not just prefixes.
*   **Clarification**: Execution logic sends HID reports immediately for modifier-only changes (`MOD_PRESS`, `MOD_RELEASE`), whether from standalone codes, `{MODIFIER_NAME}`, or explicit release (`\MOD`).
*   Other features (GridInfo, FontSize, Toggle, ActionString syntax, etc.) remain as defined previously.

## 2. File Format Overview

Configuration files are plain text files (.cfg), typically UTF-8 encoded.

*   **First Line (Mandatory): Grid Definition**
    *   **Syntax**: `<Cols>x<Rows>[ (<DefaultDelayMS>)]`
    *   **`<Cols>`**: An integer specifying the number of columns in the button grid (e.g., `5`).
    *   **`<Rows>`**: An integer specifying the number of rows in the button grid (e.g., `4`).
    *   **`(<DefaultDelayMS>)`**: (Optional, default 50) Overrides the global default delay (typically 50ms) used between sequential actions within this specific layout file. The value is specified in milliseconds within parentheses. If omitted, the global default is used.
        *   Example: `5x4` (5 columns, 4 rows, default delay)
        *   Example: `10x2 (20)` (10 columns, 2 rows, 20ms default delay for this file)

*   **Button Definitions**: Subsequent lines define buttons or are comments/empty.
    *   **Syntax**: `[T|G][<GridInfo>]<FontSize><LabelText>⭾<ActionString>`
    *   **`T` or `G`**: (Optional) The first character defines the button's primary type:
        *   **`T`**: If present, this button operates in **Toggle** mode (see Section 4.2).
        *   **`G`**: If present, this button is a **Navigation Button**. The `<ActionString>` contains the filename of the next layout to load. See Section 4.1 for details.
        *   **Absent**: If neither `T` nor `G` is present, the button operates in **Momentary** mode (see Section 4.2), and the first character is treated as part of the `<GridInfo>` if numeric, or `<FontSize>` if S, M, L, or J.
    *   **`<GridInfo>`**: (Optional) Specifies the button's placement and/or size. Must appear immediately after `T` if `T` is present. Can be:
        *   **Omitted (0 digits)**: The button defaults to a **1x1 span** and its position is determined automatically by the layout engine (see Section 3).
        *   **2 digits (`ColSpan RowSpan`)**: The button uses the specified column span (`ColSpan`) and row span (`RowSpan`). Both digits must be 1 or greater. The button's position is determined automatically. Example: `21` defines a 2-column wide, 1-row high button.
        *   **4 digits (`Col Row ColSpan RowSpan`)**: Explicitly defines the button's top-left corner position (`Col`, `Row` - 0-indexed) and its span (`ColSpan`, `RowSpan`). All span digits must be 1 or greater. Example: `0011` places a 1x1 button at the top-left (Column 0, Row 0). Example: `1121` places a 2x1 button starting at Column 1, Row 1.
    *   **`<FontSize>`**: (Mandatory) A single character specifying the font size for the label text:
        *   `S`: Small font, suitable for multiple lines or longer labels on a 1x1 button.
        *   `M`: Medium font, standard readability, suitable for 1-3 short lines on a 1x1 button.
        *   `L`: Large font, suitable for short labels (e.g., 1-2 lines of ~4 chars) on a 1x1 button.
        *   `J`: Jumbo font, optimized for displaying a single large character or icon on a 1x1 button.
    *   **`<LabelText>`**: (Mandatory) The text content displayed on the button. It starts immediately after the `<FontSize>` character. This text can contain:
        *   Standard printable ASCII characters.
        *   **Icon Codes (`$<Name>`)**: Special codes starting with `$` that are replaced by glyphs from a built-in icon font (see Appendix B). Parsing uses a **longest match** rule against Appendix B.
        *   **Literal Dollar Sign (`$$`)**: The sequence `$$` is rendered as a single literal `$` character.
        *   Example: `SOK`, `MCtrl`, `L$LEFT`, `J$OK`, `M$SAVE File`, `SPrice: $$5`.
    *   **`⭾`**: A literal tab character, represented throughout this document with this icon so it is not confused with a space. This is crucial as it separates the button's visual definition (Toggle, GridInfo, FontSize, LabelText) from its functional definition (`<ActionString>`).
    *   **`<ActionString>`**: Defines the HID keyboard actions performed by the button (see Section 4).

*   **Comment**: Lines starting with a hash symbol (`#`) are ignored by the parser.
*   **Empty Lines**: Blank lines (containing only whitespace or nothing) are ignored.

## 3. Grid, Label, and Placement

*   **Grid**: The `Cols`x`Rows` definition sets the fundamental grid structure for button placement.
*   **Label**: Defined by `<FontSize><LabelText>`.
    *   The `<FontSize>` character (`S`, `M`, `L`, `J`) selects the font size used for rendering.
    *   The `<LabelText>` provides the content. The rendering engine interprets standard ASCII characters directly, replaces recognized `$Name` codes (using longest match) with icon glyphs from Appendix B, and replaces `$$` sequences with a single `$` character. Text wrapping may occur depending on the button size, font size, and label length.
*   **Placement**: Determines where a button appears on the screen.
    *   **Explicit Placement (4-digit GridInfo: `Col Row ColSpan RowSpan`)**: Provides full control. The button's top-left corner is at (`Col`, `Row`), and it occupies `ColSpan` columns and `RowSpan` rows. Defining buttons that explicitly overlap results in a configuration error.
    *   **Automatic Placement (0 or 2-digit GridInfo)**: Offers convenience by letting the system find a suitable spot.
        *   **Span**: Determined by the 2-digit `ColSpan RowSpan` if provided, otherwise defaults to 1x1 if GridInfo is omitted.
        *   **Position**: The layout engine searches the grid cells in row-major order (left-to-right across columns, then top-to-bottom across rows). It places the button's top-left corner in the first empty cell (`r`, `c`) where the button's entire `ColSpan` x `RowSpan` area fits within the grid boundaries and does not overlap any previously placed button (whether placed explicitly or automatically earlier in the file).
        *   **Error**: If the engine cannot find a suitable empty space for an auto-placed button of the required size, a configuration error occurs.
        *   **Overlap with Automatic placement**: If an explicitly placed button attempts to occupy a cell already claimed by a previously defined auto-placed button, this also results in a configuration error. So, it is strongly suggested to place the explicitly placed buttons at the top of the file, followed by automatically placed buttons to fill in the gaps.

## 4. Action String Syntax

The `<ActionString>` defines the sequence of HID events generated when the button is interacted with (for Momentary or Toggle buttons), or the target filename (for Navigation buttons).

*   **Structure (Momentary/Toggle)**: `<Press Sequence>[|[<ms>] <Release Sequence>]`
    *   **`<Press Sequence>`**: The sequence of actions executed on the initial press (Momentary) or the first click (Toggle).
    *   **`|`**: (Optional) A pipe character separating the Press Sequence from the Release Sequence. If present, it enables explicit control over key/modifier releases.
    *   **`[(<ms>)]`**: (Optional, only if `|` is present) An explicit delay in milliseconds, enclosed in parentheses, executed *after* the release trigger (touch release or second toggle click) but *before* the `<Release Sequence>` begins. If omitted, the default delay applies here.
    *   **`<Release Sequence>`**: (Optional, only if `|` is present) The sequence of actions executed on touch release (Momentary) or the second click (Toggle). Typically used to release specific keys or modifiers held during the Press Sequence.

*   **Action Components**: The fundamental building blocks within Press or Release Sequences.
    *   `'keys'` (Simultaneous Key Press Literal)
    *   `{KEY}` (Special Key Name)
    *   `"string"` (Typing Sequence)
    *   `LC`, `LS`, etc. (Modifier Code Prefix OR Standalone Action)
    *   `\LC`, `\LS`, etc. (Explicit Modifier Release)
    *   `(<ms>)` (Explicit Delay)
    *   `G<file>` (Layout Change)
    *   `|` prefix (Release Control - only at the very start of the entire ActionString)
*   **Structure (Navigation - `G` prefix)**: `<filename.cfg>` - The entire ActionString is the filename to load.

### 4.1. Action Components Defined

*   **Simultaneous Key Press Literal (`'`)**:
    *   **Syntax**: `'keys'`
    *   **Action**: Presses the base HID keycodes corresponding to *all* characters inside the quotes *simultaneously*. These keys remain pressed until explicitly released (e.g., by the end of a Momentary action, a Toggle release, or an explicit Release Sequence).
    *   **Details**: Case is ignored for keycode mapping (`'a'` and `'A'` both press the 'A' key). Limited to 6 simultaneous non-modifier keys by the HID standard. This is a global limit across all currently active toggles and the pressed key. Simultaneous keys beyond the 6th are ignored, and may or may not become pressed once other keys are released.
    *   **Purpose**: Defining shortcuts (like `'cv'`), holding specific keys.
    *   **Examples**: `'a'`, `' '`, `''` (single quote key), `'abc'`.

*   **Special Key Name (`{}`)**:
    *   **Syntax**: `{KEY_NAME}`
    *   **Action**:
        *   If `KEY_NAME` corresponds to a non-modifier key (e.g., `ENTER`, `F1`), presses the HID keycode corresponding to that key. Generates `ACTION_COMP_TYPE_SPECIAL_KEY`. An HID report is sent.
        *   If `KEY_NAME` corresponds to a modifier key (e.g., `LCTRL`, `LSHIFT`), presses the corresponding modifier. Generates `ACTION_COMP_TYPE_MOD_PRESS`. The internal modifier state is updated, and an HID report is sent immediately.
    *   **Purpose**: Accessing non-printable keys or keys not easily represented by single characters.
    *   **Simultaneous Press**: Can be concatenated without spaces with other `{KEY}` or `'keys'` components (e.g., `'c'{DEL}`, `{LCTRL}{LALT}{DEL}` - though `LC LA {DEL}` is preferred).
    *   **Examples**: `{ENTER}`, `{F1}`, `{LEFT}`, `{KP1}`.

*   **Typing Sequence (`"`)**:
    *   **Syntax**: `"string"`
    *   **Action**: Simulates typing the string character by character. For each character (or embedded `{KEY}`): Press Key(s) -> Release Key(s) -> Default Delay.
    *   **Details**:
        *   **Case Sensitive**: Automatically handles Shift. `"A"` sends Shift+A (press/release), `"a"` sends A (press/release).
        *   **Special Keys**: `{KEY}` names can be embedded within the string (e.g., `"User:{TAB}Pass{ENTER}"`).
        *   **Timing**: The default delay applies *between* each character's press/release cycle.
        *   **Modifier Interaction**: If prefixed by modifiers (e.g., `LC"kd"`), the modifier is held *during* the sequence and released *after* the sequence completes (unless persistence rules apply). This allows complex shortcuts like Ctrl+K, D.
    *   **Purpose**: Sending pre-defined text snippets, commands, or multi-step shortcuts.
    *   **Examples**: `"Hello, World!"`, `"cd /home{ENTER}"`, `LC"kd"`.

*   **Modifier Codes (Prefix OR Standalone Action)**:
    *   **Syntax**: `LC`, `LS`, `LA`, `LG`/`LM`, `RC`, `RS`, `RA`, `RG`/`RM` (see Appendix A).
    *   **Action (Dual Role)**:
        1.  **As Prefix**: Placed immediately *before* a `'keys'`, `{KEY}`, or `"string"` component. Presses the corresponding modifier key(s) internally *before* the prefixed component executes. The modifier state is updated, but the HID report is sent by the prefixed component's action.
        2.  **As Standalone Action**: If a modifier code appears *not* immediately followed by a component it can prefix (e.g., `LC LS`, `LC (100)`, `LC |`, or `LC` at the end of a sequence), it is treated as a distinct action. It generates `ACTION_COMP_TYPE_MOD_PRESS`, updates the internal modifier state, and sends an HID report immediately containing the current modifier state.
    *   **Persistence**: When pressed (either as prefix or standalone), the modifier(s) remain active internally for subsequent sequential components in the same Press Sequence unless explicitly released (see Section 4.4).
    *   **Purpose**: Applying modifiers like Shift, Ctrl, Alt, GUI to key presses or sequences (prefix role); explicitly setting or clearing modifier state and notifying the host immediately (standalone role).
    *   **Examples**:
        *   `LS'a'` (Prefix: Shift+A, report sent with 'a')
        *   `LC'c'` (Prefix: Ctrl+C, report sent with 'c')
        *   `LA{F4}` (Prefix: Alt+F4, report sent with F4)
        *   `LG'r'` (Prefix: Win+R, report sent with 'r')
        *   `LS"hello"` (Prefix: types HELLO, reports sent per char)
        *   `LC` (Standalone: Press Left Ctrl, send report with LC modifier only)
        *   `LC LS` (Standalone LC, delay, Standalone LS. Sends report for LC, then report for LC+LS)

*   **Explicit Modifier Release (`\MOD`)**:
    *   **Syntax**: `\LC`, `\LS`, `\LA`, `\LG`/`\LM`, `\RC`, `\RS`, `\RA`, `\RG`/`\RM`.
    *   **Action**: Releases the specified modifier key(s). Generates `ACTION_COMP_TYPE_MOD_RELEASE`. The internal modifier state is updated, and an HID report is sent immediately.
    *   **Purpose**: To release a modifier mid-sequence, allowing subsequent actions without that modifier.
    *   **Example**: `LC'a' \LC 'b'` (Press Ctrl+A, release Ctrl, press B).

*   **Explicit Delay (`(<ms>)`)**:
    *   **Syntax**: `(<milliseconds>)`
    *   **Action**: Pauses execution for the specified duration. This pause occurs *after* the preceding component completes and *before* the next sequential component starts.
    *   **Details**: Overrides the default delay for this specific step in the sequence.
    *   **Purpose**: Introducing specific timing delays where needed (e.g., waiting for a UI element to appear).
    *   **Examples**: `(100)`, `(5)`, `(1000)`.

*   **Layout Change (`G<file>`)**:
    *   **Syntax**: Defined by the `G` prefix on the button definition line. The `<ActionString>` *must* contain only the target filename (e.g., `symbols.cfg`).
    *   **Action**: When a Navigation Button (defined with the `G` prefix) is clicked (pressed and released), it triggers the loading of the layout file specified in its `<ActionString>`.
    *   **Constraint**: The `<ActionString>` must contain *only* the filename. No other action components, modifiers, delays, or `|` separators are allowed. The `T` prefix cannot be used with the `G` prefix.
    *   **Implicit Reset**: Before loading the new layout, the system performs a "reset all" action:
        1. Sends an empty HID report (all keys and modifiers up) to the host.
        2. Resets the internal toggle state of *all* buttons defined in the currently loaded layout file to OFF.
        3. Sends a modifier update `0x00` via ESP-NOW (if paired) to clear remote modifier state.
    *   **Purpose**: Switching between different keyboard layouts.
    *   **Example Line**: `GM$GEAR Symbols⭾symbols.cfg` (Defines a Medium font button labeled with a gear icon and "Symbols" that navigates to `symbols.cfg` on click).

### 4.2. Action Types & Interaction Modes

Determines how the button responds to touch press and release.

*   **Momentary (Default - No `T` or `G` prefix)**:
    *   **Touch Press**: Executes the `<Press Sequence>`.
    *   **Touch Release**:
        *   If no `|` separator: Implicitly releases all keys and modifiers that were left held at the *end* of the `<Press Sequence>`. Note that keys involved in a `"string"` sequence are typically already released by the sequence itself.
        *   If `|` separator is present: Triggers the execution of the `<Release Sequence>` (after the optional `|(<ms>)` delay). No implicit release occurs.

*   **Toggle (`T` prefix at start of line)**:
    *   Acts like a latching switch, alternating state with each complete click (press and release).
    *   **First Click (Press & Release)**: Executes the `<Press Sequence>`. The button visually enters an 'active' or 'highlighted' state. The system remembers which keys/modifiers were left held at the end of the `<Press Sequence>`.
    *   **Second Click (Press & Release)**:
        *   If no `|` separator: Implicitly releases all keys and modifiers that were remembered from the first click's `<Press Sequence>`.
        *   If `|` separator is present: Triggers the execution of the `<Release Sequence>` (after the optional `|(<ms>)` delay). No implicit release occurs.
        *   The button returns to its normal visual state.
    *   **Subsequent Clicks**: Alternate between the first and second click behaviors.
*   **Navigation (`G` prefix at start of line)**:
    *   **Touch Press**: Button visually highlights.
    *   **Touch Release (Click)**: Performs the Implicit Reset actions (release HID, reset toggles, clear remote mods) and then initiates loading of the specified `<file>`.
    *   The `<ActionString>` must contain *only* the target filename.
    *   The `T` prefix cannot be used with the `G` prefix (a button is either Toggle or Navigation, not both).

### 4.3. Sequences and Timing

How components are executed relative to each other.

*   **Sequential Actions**: Components separated by one or more spaces are executed in order from left to right.
    *   **Default Delay**: The delay specified by `(<ms>)` in the grid definition (or the global default if unspecified) occurs *between* the completion of one space-separated component and the start of the next. This delay also applies between the press/release cycles of individual characters/keys within a `"string"` sequence.
    *   **Explicit Delays**: Using `(<ms>)` within the sequence overrides the default delay for that specific step.
    *   Example (`d50` default): `'a' 'b'` -> Press a, wait 50ms, Press b.
    *   Example (`d50` default): `'a' (10) 'b'` -> Press a, wait 10ms, Press b.

*   **Simultaneous Press**: Achieved by concatenating `'keys'` literals and/or `{KEY}` names *without any spaces* between them. Modifiers should prefix the entire group.
    *   Example: `'abc'` (Press a, b, c together).
    *   Example: `LC'ac'` (Press Left Ctrl, a, c together).
    *   Example: `'c'{DEL}` (Press c and Delete together).
    *   **Limitation**: `"string"` components cannot be made simultaneous with other components.

### 4.4. Modifiers and Persistence

How modifier keys (Shift, Ctrl, Alt, GUI) behave in sequences.

*   **Application**: A modifier can be activated in several ways:
    *   **Prefix**: `LC`, `LS`, etc., immediately before `'keys'`, `{KEY}`, or `"string"`. Updates internal state.
    *   **Standalone Code**: `LC`, `LS`, etc., not acting as a prefix. Updates internal state and sends an immediate report.
    *   **Special Key Name**: `{LCTRL}`, `{LSHIFT}`, etc. Updates internal state and sends an immediate report.
*   **Execution**: When a modifier state changes (press via standalone code, `{NAME}`, or release via `\MOD`), the internal modifier state (`current_modifier_mask`) is updated, and an HID report reflecting the *new* state is sent immediately. When a modifier is applied as a prefix, the internal state is updated, but the report is sent by the subsequent key action.
*   **Persistence Rule**: Once a modifier key is pressed (by any method), it **remains held down** internally for any subsequent sequential components within the *same* `<Press Sequence>`, *unless* it is explicitly released with the corresponding `\MOD` component (e.g., `\LC`).
*   **Implicit Release at End**: Modifiers still active internally when the `<Press Sequence>` and `<Release Sequence>` (if present) conclude are released (see Sections 4.2 and 4.5).
*   **Examples**:
    *   `LS'a'` -> Press LS (internal), Press a (report: LS+a). (LS+a released at end).
    *   `LS 'a'` -> Press LS (standalone, report: LS), (delay), Press a (report: LS+a). (LS+a released at end).
    *   `LS'a' 'b'` -> Press LS (internal), Press a (report: LS+a), (delay), Press b (report: LS+a+b). (LS+a+b released at end).
    *   `LS'a' \LS 'b'` -> Press LS (internal), Press a (report: LS+a), (delay), Release LS (report: a), (delay), Press b (report: a+b). (Only b released at end). *Correction: Report after \LS should just be 'a'.* -> **Correction:** Report after `\LS` should be just 'a' if 'a' is still held. If implicit release applies, it might be empty. Let's assume 'a' is held until end/explicit release. Report after `\LS` is `a`. Report after `'b'` is `a+b`.
    *   `LC'a' LS'b'` -> Press LC (internal), Press a (report: LC+a), (delay), Press LS (internal), Press b (report: LC+LS+a+b). (LC+LS+a+b released at end).
    *   `LC"kd"` -> Press LC (internal), Type K (report: LC+k press, delay, report: LC release), (delay), Type D (report: LC+d press, delay, report: LC release), (delay), Release LC (internal, no report needed here if string logic handles final release). *String logic needs careful implementation.*
    *   `LC{LCTRL}` -> Press LC (internal), Press LCTRL (standalone, report: LC). No practical difference from just `LC`.
    *   `LC LS 'a'` -> Press LC (standalone, report: LC), (delay), Press LS (standalone, report: LC+LS), (delay), Press a (report: LC+LS+a).

### 4.5. Explicit Release Sequence (`|` separator)

Provides fine-grained control over what happens upon release/toggle-off.

*   **Format**: `<Press Sequence> |[(<ms>)] <Release Sequence>`
*   **`|`**: The pipe character enables this feature.
*   **`|(<ms>)`**: An optional explicit delay occurring *after* the trigger (touch release/toggle-off) and *before* the `<Release Sequence>` starts. Uses default delay if omitted.
*   **`<Release Sequence>`**: A sequence of Action Components defining what should happen upon release. Crucially, this sequence typically specifies which keys or modifiers (using their codes like `LC`, `LS`, `'a'`, `{DEL}`) should be *released*. It follows the same syntax rules as the `<Press Sequence>` regarding components and delays. `"string"` components are rarely useful here.
*   **Responsibility**: If you use `|`, you are responsible for explicitly releasing *all* keys and modifiers held by the `<Press Sequence>` that you want released. No implicit release occurs for keys held from the `<Press Sequence>`.
*   **Triggering**: Activated by touch release (Momentary mode) or the second click (Toggle mode).
*   **Examples**:
    *   `LS'c' |(0) 'c' LS` -> Press Shift+C. On release: 0ms delay, Release C, default delay, Release Shift.
    *   `'A' (200) | 'A'` -> Press A, hold min 200ms. On release: default delay, Release A.
    *   `LC'k' 'd' | 'd' (0) 'k' (0) LC` -> Press LC+K, delay, Press D [LC held]. On release: default delay, Release D, 0ms, Release K, 0ms, Release LC.

## 5. Execution Flow Summary

1.  **Parse**: Read the `.cfg` file line by line.
    *   Parse Grid Definition: Store `Cols`, `Rows`, `DefaultDelayMS`.
    *   For each Button Definition line:
        *   Check for leading `T` (Toggle mode).
    *   Check for leading `G` (Navigation mode). If `G` is present, `T` cannot be.
        *   Check for GridInfo (0, 2, or 4 digits). Store placement/span info and placement type (explicit/auto). Remember `Col Row ColSpan RowSpan` order for 4 digits.
        *   Expect and store mandatory Font Size character (`S`, `M`, `L`, or `J`).
        *   Extract LabelText up to `⭾`. Parse for ASCII, `$Name` icon codes (longest match against Appendix B), and `$$` escapes, storing the sequence of literal segments and icon codes.
        *   Expect `⭾`.
    *   If `G` prefix: Parse the `<ActionString>` as the target filename. Store it.
    *   If `T` or no prefix (Momentary): Parse the `<ActionString>` into its sequence of Press Components and (if present) Release Components, noting explicit delays and the `|` separator. Store the parsed action sequences.
2.  **Layout and Rendering (Single Pass)**: As each button definition line is parsed:
    *   Determine the button's position and span based on its GridInfo (explicit or auto).
    *   For auto-placed buttons, find the next available slot in the grid (respecting the ordering constraint that explicit buttons are already placed). Check for overlaps or out-of-bounds errors.
    *   For each button, use its calculated position and span.
    *   Create the LVGL button and label objects.
    *   Render the stored label sequence (literal text segments and icon codes) using the stored `FontSize`. Replace icon codes with glyphs from Appendix B, render `$$` as `$`. Handle text wrapping within the button bounds.
4.  **Runtime Execution**: When a button touch event occurs:
    *   **Touch Press**:
        *   Check for `|` prefix (`|Modifiers` or `|X`) and execute immediately if present.
        *   If Momentary (` ` prefix): Execute the `<Press Sequence>`, respecting delays and modifier persistence rules.
        *   If Toggle (`T` prefix): Do nothing (wait for release).
        *   If Navigation (`G` prefix): Visually highlight the button.
    *   **Touch Release**:
        *   If Momentary (` ` prefix):
            *   No `|`: Implicitly release keys/modifiers left held at the end of the `<Press Sequence>`.
            *   Has `|`: Execute the `<Release Sequence>` (after optional `|(<ms>)` delay).
        *   If Toggle mode (`T`):
            *   If button state is OFF: Execute `<Press Sequence>`, change state to ON, highlight button, remember held keys/modifiers.
            *   If button state is ON:
                *   No `|`: Implicitly release keys/modifiers remembered from the ON state activation.
                *   Has `|`: Execute the `<Release Sequence>` (after optional `|(<ms>)` delay).
                *   Change state to OFF, unhighlight button.
    *   **Layout Change (`G<file>`)**: If encountered during sequence execution, load the new layout file after the current component finishes.
        *   If Navigation (`G` prefix): Perform the Implicit Reset actions (release HID, reset toggles, clear remote mods) and then initiate loading of the specified `<file>` stored from the `<ActionString>`.

## 6. Examples (v3.12 Syntax)

*Assume a 5x4 grid and default delay of 50ms unless specified otherwise.*

*   **Explicit Placement (4 digits: `Col Row ColSpan RowSpan`)**:
    *   `0011MA⭾'a'`
    *   `T1121MCtrl⭾LC`
*   **Auto Placement - Default Span (0 digits)**:
    *   `MQ⭾'q'`
    *   `TMW⭾'w'`
    *   `ME⭾'e'`
*   **Auto Placement - Specified Span (2 digits: `ColSpan RowSpan`)**:
    *   `21LEnter⭾{ENTER}`
    *   `12MShift⭾LS`
    *   `T22SBig Toggle⭾"Toggle Me"`
*   **Using Icon Codes & Escapes (Referencing updated Appendix B)**:
    *   `J$UP⭾{UP}` (Jumbo Up Arrow Icon)
    *   `M$OK OK⭾{ENTER}` (Medium Check icon followed by literal " OK")
    *   `S$SAVE File⭾LC's'` (Small Save icon followed by literal " File")
    *   `MPrice: $$5.00⭾"5.00"` (Medium font, literal "Price: $5.00")
    *   `S$$$$ Top Price $$$$⭾"'1'"` (Small font, literal "\$$ Top Price \$$")
    *   `J$AUDIO⭾{VOLUP}` (Jumbo Audio/Sound icon)
    *   `M$WIFI Status⭾Gwifi.cfg` (Medium Wifi icon + " Status")
*   **Mixed Placement Example**: (Using `G` prefix for navigation) Here we use an actual tab instead of ⭾ to show you what the file will look like.
    ```
    5x4 (20)
    # Explicit placements first (Col Row ColSpan RowSpan)
    0011MA  'a'
    1011MB  'b'
    4011MC  'c'
    0111MD  'd'
    # Auto placements fill the gaps
    ME  'e'      # Auto 1x1 placed at 2,0
    MF  'f'      # Auto 1x1 placed at 3,0
    21S$COPY Copy⭾LC'c' # Auto 2x1 placed at 1,1, Small Font, Copy icon + " Copy"
    MJ  'j'      # Auto 1x1 placed at 3,1
    MK  'k'      # Auto 1x1 placed at 4,1
    22J$HOME    {HOME} # Auto 2x2 placed at 0,2, Large Font Home Icon
    G2221MSymbols⭾symbols.cfg # Auto 1x1 placed at 2,2, Nav button
    ML  'l'      # Auto 1x1 placed at 4,2
    ```
    This layout will look like this:
    ```
    (Col 0) (1)   (2)   (3)   (4)
    +-----+-----+-----+-----+-----+
    |  A  |  B  |  E  |  F  |  C  |  (Row 0)
    +-----+-----+-----+-----+-----+
    |  D  |  🗊 Copy  |  J  |  K  |  (Row 1)
    +-----+-----------+-----+-----+
    |    ,^、   |  Symbols  |  L  |  (Row 2)
    |   /---\   +-----+-----+-----+
    |   |_‖_|   |     |     |     |  (Row 3)
    +-----------+-----+-----+-----+
    ```
*   **Simultaneous Press**:
    *   `0011MCut⭾LC'x'` (Explicit placement)
    *   `MCopy⭾LC'c'` (Auto placement 1x1)
    *   `MPaste⭾LC'v'` (Auto placement 1x1)
*   **Sequential Key Presses**:
    *   `MSeq⭾'q' (10) 'w' (100) 'e'` (Auto placement 1x1)
    *   `MSeqDef⭾'q' 'w' 'e'` (Auto placement 1x1, uses default delay)
*   **Typing Strings**:
    *   `MGreet⭾"Hello World!"` (Auto placement 1x1)
    *   `TSCode Snippet⭾"if (x > 0) {NL}{TAB}// TODO{NL}"` (Toggle, Auto placement 1x1, Small Font)
*   **Modifiers & Persistence**:
    *   `MShift1⭾LS'1'` (Auto placement 1x1, sends '!')
    *   `MCapsHello⭾LS"hello"` (Auto placement 1x1, sends 'HELLO')
    *   `MCtrlK D⭾LC"kd"` (Auto placement 1x1, sends Ctrl+K, D sequence)
    *   `MCtrlA B⭾LC'a' 'b'` (Auto placement 1x1, sends Ctrl+A, then Ctrl+B)
    *   `MCtrlA then B⭾LC'a' \LC 'b'` (Auto placement 1x1, sends Ctrl+A, then just B)
*   **Explicit Release**:
    *   `MHoldA⭾'A' (500) | 'A'` (Auto placement 1x1)
    *   `TMHoldA⭾T'A' (500) |(20) 'A'` (Toggle, Auto placement 1x1)
*   **Navigation Buttons (G prefix)**:
    *   `G0311SBack⭾main.cfg` (Explicit 1x1 at 0,3, Small font, Navigates to main.cfg)
    *   `G21MUtils⭾utils.cfg` (Auto 2x1, Medium font, Navigates to utils.cfg)
    *   `GJ$LEFT⭾numpad.cfg` (Auto 1x1, Jumbo font Left Arrow icon, Navigates to numpad.cfg)

## Appendix A: Special Key Names (`{NAME}`) and Modifier Codes (Prefixes/Standalone)

**(Content updated for v3.12 to match `hid_dev.h` constants)**

**Modifier Codes (Prefixes & Release):**

| Code | Modifier       | Release Code | Hex Mask | Notes                                   |
| :--- | :------------- | :----------- | :------- | :-------------------------------------- |
| LC   | Left Control   | \LC          | 0x01     | Prefix applies, persists. `\` releases. |
| LS   | Left Shift     | \LS          | 0x02     | Prefix applies, persists. `\` releases. |
| LA   | Left Alt       | \LA          | 0x04     | Prefix applies, persists. `\` releases. |
| LG   | Left GUI       | \LG          | 0x08     | Prefix applies, persists. `\` releases. |
| LM   | Left Meta      | \LM          | 0x08     | (Alias for LG)                          |
| RC   | Right Control  | \RC          | 0x10     | Prefix applies, persists. `\` releases. |
| RS   | Right Shift    | \RS          | 0x20     | Prefix applies, persists. `\` releases. |
| RA   | Right Alt      | \RA          | 0x40     | Prefix applies, persists. `\` releases. |
| RG   | Right GUI      | \RG          | 0x80     | Prefix applies, persists. `\` releases. |
| RM   | Right Meta     | \RM          | 0x80     | (Alias for RG)                          |

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

*Note: Modifier codes (LC, LS etc.) can act as prefixes affecting subsequent actions OR as standalone actions that update the modifier state and send an immediate report. Special key names like {LCTRL}, {LSHIFT} also update the state and send an immediate report.*

## Appendix B: Icon Codes (`$<Name>`)

Icon codes provide access to a subset of FontAwesome icons built into the default LVGL fonts, corresponding to the `LV_STR_SYMBOL_...` definitions. Use these codes within the `<LabelText>` part of a button definition. The rendering engine will replace the code (e.g., `$OK`) with the corresponding icon glyph.


**Parsing Rule:** When parsing `<LabelText>`, the system uses a **longest match** rule. It looks for the longest possible sequence starting with `$` that matches an Icon Code name defined below. Any characters immediately following the matched code are treated as literal text.

**Escape Sequence:**
*   Use `$$` to represent a literal `$` character.

*(Note: The exact appearance depends on the font configured in the firmware.)*

| Icon Code (`$<Name>`) | Corresponding LVGL STR   | Description             |
| :-------------------- | :----------------------- | :---------------------- |
| `$BULLET`             | `LV_SYMBOL_BULLET`       | Bullet point (•)        |
| `$AUDIO`              | `LV_SYMBOL_AUDIO`        | Audio / Sound           |
| `$VIDEO`              | `LV_SYMBOL_VIDEO`        | Video / Camera          |
| `$LIST`               | `LV_SYMBOL_LIST`         | List / Menu             |
| `$OK`                 | `LV_SYMBOL_OK`           | Check mark              |
| `$CLOSE`              | `LV_SYMBOL_CLOSE`        | Close / Cancel (X)      |
| `$POWER`              | `LV_SYMBOL_POWER`        | Power symbol            |
| `$SETTINGS`           | `LV_SYMBOL_SETTINGS`     | Settings / Gear         |
| `$TRASH`              | `LV_SYMBOL_TRASH`        | Trash can               |
| `$HOME`               | `LV_SYMBOL_HOME`         | Home icon               |
| `$DOWNLOAD`           | `LV_SYMBOL_DOWNLOAD`     | Download arrow          |
| `$DRIVE`              | `LV_SYMBOL_DRIVE`        | Drive / HDD             |
| `$REFRESH`            | `LV_SYMBOL_REFRESH`      | Refresh / Reload        |
| `$MUTE`               | `LV_SYMBOL_MUTE`         | Volume muted            |
| `$VOLUME_MID`         | `LV_SYMBOL_VOLUME_MID`   | Volume medium           |
| `$VOLUME_MAX`         | `LV_SYMBOL_VOLUME_MAX`   | Volume high             |
| `$IMAGE`              | `LV_SYMBOL_IMAGE`        | Image / Picture         |
| `$EDIT`               | `LV_SYMBOL_EDIT`         | Edit / Pencil           |
| `$PREV`               | `LV_SYMBOL_PREV`         | Previous track          |
| `$PLAY`               | `LV_SYMBOL_PLAY`         | Play button             |
| `$PAUSE`              | `LV_SYMBOL_PAUSE`        | Pause button            |
| `$STOP`               | `LV_SYMBOL_STOP`         | Stop button             |
| `$NEXT`               | `LV_SYMBOL_NEXT`         | Next track              |
| `$EJECT`              | `LV_SYMBOL_EJECT`        | Eject                   |
| `$LEFT`               | `LV_SYMBOL_LEFT`         | Left arrow              |
| `$RIGHT`              | `LV_SYMBOL_RIGHT`        | Right arrow             |
| `$PLUS`               | `LV_SYMBOL_PLUS`         | Plus sign               |
| `$MINUS`              | `LV_SYMBOL_MINUS`        | Minus sign              |
| `$EYE_OPEN`           | `LV_SYMBOL_EYE_OPEN`     | Eye open                |
| `$EYE_CLOSE`          | `LV_SYMBOL_EYE_CLOSE`    | Eye closed              |
| `$WARNING`            | `LV_SYMBOL_WARNING`      | Warning / Exclamation   |
| `$SHUFFLE`            | `LV_SYMBOL_SHUFFLE`      | Shuffle arrows          |
| `$UP`                 | `LV_SYMBOL_UP`           | Up arrow                |
| `$DOWN`               | `LV_SYMBOL_DOWN`         | Down arrow              |
| `$LOOP`               | `LV_SYMBOL_LOOP`         | Loop arrows             |
| `$DIRECTORY`          | `LV_SYMBOL_DIRECTORY`    | Folder / Directory      |
| `$UPLOAD`             | `LV_SYMBOL_UPLOAD`       | Upload arrow            |
| `$CALL`               | `LV_SYMBOL_CALL`         | Call / Phone            |
| `$CUT`                | `LV_SYMBOL_CUT`          | Cut / Scissors          |
| `$COPY`               | `LV_SYMBOL_COPY`         | Copy icon               |
| `$SAVE`               | `LV_SYMBOL_SAVE`         | Save icon (Floppy)      |
| `$CHARGE`             | `LV_SYMBOL_CHARGE`       | Battery charging / Bolt |
| `$PASTE`              | `LV_SYMBOL_PASTE`        | Paste icon              |
| `$BELL`               | `LV_SYMBOL_BELL`         | Bell icon               |
| `$KEYBOARD`           | `LV_SYMBOL_KEYBOARD`     | Keyboard icon           |
| `$GPS`                | `LV_SYMBOL_GPS`          | GPS / Location          |
| `$FILE`               | `LV_SYMBOL_FILE`         | File icon               |
| `$WIFI`               | `LV_SYMBOL_WIFI`         | WiFi symbol             |
| `$BATTERY_FULL`       | `LV_SYMBOL_BATTERY_FULL` | Battery full            |
| `$BATTERY_3`          | `LV_SYMBOL_BATTERY_3`    | Battery 3/4             |
| `$BATTERY_2`          | `LV_SYMBOL_BATTERY_2`    | Battery 1/2             |
| `$BATTERY_1`          | `LV_SYMBOL_BATTERY_1`    | Battery 1/4             |
| `$BATTERY_EMPTY`      | `LV_SYMBOL_BATTERY_EMPTY`| Battery empty           |
| `$USB`                | `LV_SYMBOL_USB`          | USB symbol              |
| `$BLUETOOTH`          | `LV_SYMBOL_BLUETOOTH`    | Bluetooth symbol        |
| `$BACKSPACE`          | `LV_SYMBOL_BACKSPACE`    | Backspace arrow         |
| `$SD_CARD`            | `LV_SYMBOL_SD_CARD`      | SD Card icon            |
| `$NEW_LINE`           | `LV_SYMBOL_NEW_LINE`     | New line / Return arrow |
