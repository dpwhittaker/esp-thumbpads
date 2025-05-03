# Thumbpad Keyboard Configuration Language v3.12

## 1. Introduction

This document describes the configuration file format (v3.12) used to define keyboard layouts and actions for the ESP32 Thumbpad device. This format allows for defining button appearance (including font size and **icon codes**), grid layout (explicitly or automatically with optional sizing), and complex HID keyboard actions including **sequential character typing (`"string"`)**, **simultaneous key presses (`'keys'`)**, default and explicit delays using `(<ms>)` syntax, toggles, modifiers with defined persistence, explicit modifier release (`\MOD`), and explicit key release control (`|`).

**Key changes in v3.12:**
*   **Corrected Appendix B (Icon Codes)** to accurately reflect symbols defined by `LV_STR_SYMBOL_...` constants.
*   Escape sequence for a literal dollar sign in label text remains `$$`.
*   Parsing rule for Icon Codes (`$<Name>`) remains longest match.
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
    *   **Syntax**: `[T][<GridInfo>]<FontSize><LabelText>⭾<ActionString>`
    *   **`T`**: (Optional) If present as the *very first character* on the line, this button operates in **Toggle** mode. If absent, the button operates in **Momentary** mode (see Section 4.2).
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
        *   **Overlap with Automatic placement**: If an explicitly placed button attempts to occupy a cell already claimed by a previously defined auto-placed button, this also results in a configuration error.  So, it is strongly suggested to place the explicitly placed buttons at the top of the file, followed by automatically placed buttons to fill in the gaps.

## 4. Action String Syntax

The `<ActionString>` defines the sequence of HID events generated when the button is interacted with. It consists of one or more *Action Components* separated by spaces. Spaces imply the **default delay** (defined in the grid header or globally) between the completion of one component and the start of the next, unless an explicit delay `(<ms>)` is used.

*   **Overall Structure**: `<Press Sequence>[|[<ms>] <Release Sequence>]`
    *   **`<Press Sequence>`**: The sequence of actions executed on the initial press (Momentary) or the first click (Toggle).
    *   **`|`**: (Optional) A pipe character separating the Press Sequence from the Release Sequence. If present, it enables explicit control over key/modifier releases.
    *   **`[(<ms>)]`**: (Optional, only if `|` is present) An explicit delay in milliseconds, enclosed in parentheses, executed *after* the release trigger (touch release or second toggle click) but *before* the `<Release Sequence>` begins. If omitted, the default delay applies here.
    *   **`<Release Sequence>`**: (Optional, only if `|` is present) The sequence of actions executed on touch release (Momentary) or the second click (Toggle). Typically used to release specific keys or modifiers held during the Press Sequence.

*   **Action Components**: The fundamental building blocks within Press or Release Sequences.
    *   `'keys'` (Simultaneous Key Press Literal)
    *   `{KEY}` (Special Key Name)
    *   `"string"` (Typing Sequence)
    *   `LC`, `LS`, etc. (Modifier Code Prefix)
    *   `\LC`, `\LS`, etc. (Explicit Modifier Release)
    *   `(<ms>)` (Explicit Delay)
    *   `G<file>` (Layout Change)
    *   `|` prefix (Release Control - only at the very start of the entire ActionString)

### 4.1. Action Components Defined

*   **Simultaneous Key Press Literal (`'`)**:
    *   **Syntax**: `'keys'` - One or more printable ASCII characters enclosed in single quotes.
    *   **Action**: Presses the base HID keycodes corresponding to *all* characters inside the quotes *simultaneously*. These keys remain pressed until explicitly released (e.g., by the end of a Momentary action, a Toggle release, or an explicit Release Sequence).
    *   **Details**: Case is ignored for keycode mapping (`'a'` and `'A'` both press the 'A' key). Limited to 6 simultaneous non-modifier keys by the HID standard.
    *   **Purpose**: Defining shortcuts (like `'cv'`), holding specific keys.
    *   **Examples**: `'a'`, `' '`, `''` (single quote key), `'abc'`.

*   **Special Key Name (`{}`)**:
    *   **Syntax**: `{KEY_NAME}` - A recognized special key name enclosed in curly braces (see Appendix A).
    *   **Action**: Presses the HID keycode corresponding to the special key (e.g., Enter, F1, Delete, Arrow Keys). The key remains pressed until explicitly released.
    *   **Purpose**: Accessing non-printable keys or keys not easily represented by single characters.
    *   **Simultaneous Press**: Can be concatenated without spaces with other `{KEY}` or `'keys'` components (e.g., `'c'{DEL}`, `{LCTRL}{LALT}{DEL}` - though `LC LA {DEL}` is preferred).
    *   **Examples**: `{ENTER}`, `{F1}`, `{LEFT}`, `{KP1}`.

*   **Typing Sequence (`"`)**:
    *   **Syntax**: `"string"` - One or more characters enclosed in double quotes.
    *   **Action**: Simulates typing the string character by character. For each character (or embedded `{KEY}`): Press Key(s) -> Release Key(s) -> Default Delay.
    *   **Details**:
        *   **Case Sensitive**: Automatically handles Shift. `"A"` sends Shift+A (press/release), `"a"` sends A (press/release).
        *   **Special Keys**: `{KEY}` names can be embedded within the string (e.g., `"User:{TAB}Pass{ENTER}"`).
        *   **Timing**: The default delay applies *between* each character's press/release cycle.
        *   **Modifier Interaction**: If prefixed by modifiers (e.g., `LC"kd"`), the modifier is held *during* the sequence and released *after* the sequence completes (unless persistence rules apply). This allows complex shortcuts like Ctrl+K, D.
    *   **Purpose**: Sending pre-defined text snippets, commands, or multi-step shortcuts.
    *   **Examples**: `"Hello, World!"`, `"cd /home{ENTER}"`, `LC"kd"`.

*   **Modifier Codes (Prefix)**:
    *   **Syntax**: `LC`, `LS`, `LA`, `LG`/`LM`, `RC`, `RS`, `RA`, `RG`/`RM` (see Appendix A).
    *   **Action**: Placed immediately *before* a `'keys'`, `{KEY}`, or `"string"` component. Presses the corresponding modifier key(s).
    *   **Persistence**: The modifier(s) remain pressed for subsequent sequential components in the same Press Sequence unless explicitly released or overridden (see Section 4.4).
    *   **Purpose**: Applying modifiers like Shift, Ctrl, Alt, GUI to key presses or sequences.
    *   **Examples**: `LS'a'` (Shift+A), `LC'c'` (Ctrl+C), `LA{F4}` (Alt+F4), `LG'r'` (Win+R), `LS"hello"` (types HELLO).

*   **Explicit Modifier Release (`\MOD`)**:
    *   **Syntax**: `\LC`, `\LS`, `\LA`, `\LG`/`\LM`, `\RC`, `\RS`, `\RA`, `\RG`/`\RM`. A backslash followed by a modifier code.
    *   **Action**: Releases the specified modifier key(s).
    *   **Purpose**: To release a modifier mid-sequence, allowing subsequent actions without that modifier.
    *   **Example**: `LC'a' \LC 'b'` (Press Ctrl+A, release Ctrl, press B).

*   **Explicit Delay (`(<ms>)`)**:
    *   **Syntax**: `(<milliseconds>)` - An integer number of milliseconds enclosed in parentheses.
    *   **Action**: Pauses execution for the specified duration. This pause occurs *after* the preceding component completes and *before* the next sequential component starts.
    *   **Details**: Overrides the default delay for this specific step in the sequence.
    *   **Purpose**: Introducing specific timing delays where needed (e.g., waiting for a UI element to appear).
    *   **Examples**: `(100)`, `(5)`, `(1000)`.

*   **Layout Change (`G<file>`)**:
    *   **Syntax**: `G<filename.cfg>` - The letter `G` followed immediately by the filename of another layout configuration file.
    *   **Action**: After completing all preceding actions in the current sequence component, unload the current layout and load the specified layout file.
    *   **Purpose**: Switching between different keyboard layouts (e.g., main, symbols, numpad).
    *   **Example**: `Gsymbols.cfg`, `Gmain.cfg`.

### 4.2. Action Types & Interaction Modes

Determines how the button responds to touch press and release.

*   **Momentary (Default - No `T` prefix)**:
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

*   **Application**: A modifier code (`LC`, `LS`, etc.) applies its effect (pressing the modifier key) just before the *immediately following* action component (`'keys'`, `{KEY}`, or `"string"`).
*   **Persistence Rule**: Once a modifier key is pressed via a modifier code prefix, it **remains held down** for any subsequent sequential components within the *same* `<Press Sequence>`, *unless* one of the following occurs:
    1.  **Explicit Release**: An `\MOD` component (e.g., `\LC`) explicitly releases that modifier.
    2.  **Overridden/Reapplied**: Another modifier code affecting the *same* physical key (e.g., `RC` after `LC`, or even `LC` again) is encountered. This resets the state for the new component (effectively releasing and re-pressing, or just continuing to hold if the same modifier is reapplied).
    3.  **Sequence End**: The `<Press Sequence>` finishes.
*   **Implicit/Explicit Release at End**: Modifiers still active when the `<Press Sequence>` concludes are released according to the button's mode and whether an explicit `<Release Sequence>` is defined (see Sections 4.2 and 4.5).
*   **Typing Sequence (`"string"`) Exception**: Modifiers prefixed to a `"string"` component are generally held *only* for the duration of that typing sequence and are automatically released immediately after it finishes, *unless* the very next component in the sequence explicitly requires the same modifier to remain held (via persistence or another prefix). This allows `LC"kd"` to work as expected (Ctrl held only for K and D typing) while still allowing `LC"k" LC'd'` (Ctrl held for K typing, released, then Ctrl pressed again for D press).
*   **Examples**:
    *   `LS'a'` -> Press LS, Press a. (LS+a released at end).
    *   `LS'a' 'b'` -> Press LS, Press a, (delay), Press b [LS still held]. (LS+a+b released at end).
    *   `LS'a' \LS 'b'` -> Press LS, Press a, (delay), Release LS, (delay), Press b. (Only b released at end).
    *   `LC'a' LS'b'` -> Press LC, Press a, (delay), Press LS, Press b [LC still held]. (LC+LS+a+b released at end).
    *   `LC"kd"` -> Press LC, Type K, (delay), Type D, (delay), Release LC.
    *   `LC"k" 'd'` -> Press LC, Type K, (delay), Release LC, (delay), Press d. (d released at end).
    *   `LC'k' 'd'` -> Press LC, Press K, (delay), Press D [LC still held]. (LC+K+D released at end).

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

### 4.6. Release Control Prefix (`|` at start)

Special actions executed immediately on touch *press*, primarily for resetting state.

*   **Format**: `|<Release Modifiers><ActionString>` or `|X<ActionString>`
*   **`|Modifiers`**: Placed at the very beginning. Releases the specified modifier keys *immediately* upon touch press, before any other part of the `<ActionString>` executes. Example: `|LSLG Gmenu.cfg` -> On press, release Left Shift and Left GUI, then execute `Gmenu.cfg`.
*   **`|X`**: Placed at the very beginning. Performs a "panic" or "reset all" action immediately upon touch press:
    1.  Sends an empty HID report (all keys and modifiers up) to the host.
    2.  Resets the internal toggle state of *all* buttons defined in the currently loaded layout file to OFF.
    3.  Sends a modifier update `0x00` via ESP-NOW (if paired) to clear remote modifier state.
    4.  Proceeds to execute the rest of the `<ActionString>` (if any).
    *   Example: `|X Gmain.cfg` -> On press, release everything, reset toggles, clear remote mods, then load `main.cfg`.

## 5. Execution Flow Summary

1.  **Parse**: Read the `.cfg` file line by line.
    *   Parse Grid Definition: Store `Cols`, `Rows`, `DefaultDelayMS`.
    *   For each Button Definition line:
        *   Check for leading `T` (Toggle mode).
        *   Check for GridInfo (0, 2, or 4 digits). Store placement/span info and placement type (explicit/auto). Remember `Col Row ColSpan RowSpan` order for 4 digits.
        *   Expect and store mandatory Font Size character (`S`, `M`, `L`, or `J`).
        *   Extract LabelText up to `⭾`. Parse for ASCII, `$Name` icon codes (longest match against Appendix B), and `$$` escapes, storing the sequence of literal segments and icon codes.
        *   Expect `⭾`.
        *   Parse the `<ActionString>` into its sequence of Press Components and (if present) Release Components, noting explicit delays and the `|` separator.
2.  **Layout and Rendering (Single Pass)**: As each button definition line is parsed:
    *   Determine the button's position and span based on its GridInfo (explicit or auto).
    *   For auto-placed buttons, find the next available slot in the grid (respecting the ordering constraint that explicit buttons are already placed). Check for overlaps or out-of-bounds errors.
    *   For each button, use its calculated position and span.
    *   Create the LVGL button and label objects.
    *   Render the stored label sequence (literal text segments and icon codes) using the stored `FontSize`. Replace icon codes with glyphs from Appendix B, render `$$` as `$`. Handle text wrapping within the button bounds.
4.  **Runtime Execution**: When a button touch event occurs:
    *   **Touch Press**:
        *   Check for `|` prefix (`|Modifiers` or `|X`) and execute immediately if present.
        *   If Momentary mode: Execute the `<Press Sequence>`, respecting delays and modifier persistence rules.
        *   If Toggle mode: Do nothing (wait for release).
    *   **Touch Release**:
        *   If Momentary mode:
            *   No `|`: Implicitly release keys/modifiers left held by the `<Press Sequence>`.
            *   Has `|`: Execute the `<Release Sequence>` (after optional `|(<ms>)` delay).
        *   If Toggle mode (`T`):
            *   If button state is OFF: Execute `<Press Sequence>`, change state to ON, highlight button, remember held keys/modifiers.
            *   If button state is ON:
                *   No `|`: Implicitly release keys/modifiers remembered from the ON state activation.
                *   Has `|`: Execute the `<Release Sequence>` (after optional `|(<ms>)` delay).
                *   Change state to OFF, unhighlight button.
    *   **Layout Change (`G<file>`)**: If encountered during sequence execution, load the new layout file after the current component finishes.

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
*   **Mixed Placement Example**: Here we use an actual tab instead of ⭾ to show you what the file will look like.
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
    ML  'l'      # Auto 1x1 placed at 2,2
    ```
    This layout will look like this:
    ```
    (Col 0) (1)   (2)   (3)   (4)
    +-----+-----+-----+-----+-----+
    |  A  |  B  |  E  |  F  |  C  |  (Row 0)
    +-----+-----+-----+-----+-----+
    |  D  |  🗊 Copy  |  J  |  K  |  (Row 1)
    +-----+-----------+-----+-----+
    |    ,^、   |  L  |     |     |  (Row 2)
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
*   **Release Control & Layout Change**:
    *   `MPanic⭾|X Gmain.cfg` (Auto placement 1x1)
    *   `MDone⭾"Done." (500) Gmain.cfg` (Auto placement 1x1)


## Appendix A: Special Key Names (`{NAME}`) and Modifier Codes (Prefixes)

**(Content remains the same as v3.7 - listing Modifier Codes and Special Key Names with Hex codes)**

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
