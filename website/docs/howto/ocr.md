## How OCR reaches the program

Any OCR program supporting "post-capture actions" can integrate seamlessly with MicroDict.

A few examples are provided below, though many similar tools are available.

## Clipboard Hotkey Mode

This is the universal method: it works with any OCR program that can copy its
recognition result to the system clipboard.

1. **Configure your OCR program** to copy the recognized text to the clipboard after
   recognition — Capture2Text, OCRSpace, ScreenTranslate and most others can do this.
2. **Press MicroDict's global hotkey** (`F12` by default). The main window opens and
   the clipboard contents are searched.

On Wayland the clipboard is readable only by the focused window, which is why the
hotkey opens the window first and reads the clipboard immediately afterwards.

### Benefits

- Works with any OCR tool that can write to the clipboard
- No command-line arguments or executable paths to configure
## Capture2Text

Capture2Text can call executable after capturing, and you can set the executable to MicroDict.

Detailed usage document: [Capture2Text](https://capture2text.sourceforge.net/)

[Capture2Text Download](https://github.com/xiaoyifang/Capture2Text/releases/tag/prerelease-20220806)

For example, change the Output action `Call Executable` to the program you run:

```
/usr/local/bin/microdict "${capture}"
```

The full path of an AppImage works just as well — it is an executable file, so it can
be the command.

Then press <kbd>Win+Q</kbd> and select a region. After capturing a word, Capture2Text forwards it to the running MicroDict, which looks it up in its main window.

The hotkeys can be configured.

Capture2Text can also obtain word near the cursor without selecting a region via the "Forward Text Line Capture" by pressing <kbd>Win+W</kbd>

you may want to enable "First word only" so that only a single word would be captured

### Use Capture2Text on Linux

Capture2Text does not have a Linux version, but I have ported it to Linux <https://github.com/xiaoyifang/Capture2Text> thanks to [Capture2Text Linux Port](https://github.com/GSam/Capture2Text ) and to
sikmir, who reported the upstream build problem.

## Tesseract via command line

On Linux, you can combine command line screenshot then pass the output image to Tesseract then pass the text result to `microdict`

Example with spectacle (KDE) and grim (wayland/wlroots)

```
#!/usr/bin/env bash

set -e

case $DESKTOP_SESSION in
    sway)
        grim -g "$(slurp)" /tmp/tmp.just_random_name.png
    ;;
    plasmawayland | plasma)
        spectacle --region --nonotify --background \
        --output /tmp/tmp.just_random_name.png
    ;;
    *)
        echo "Failed to know desktop type"
        exit 1
    ;;
esac

# note that tesseract will apppend .txt to output file
tesseract /tmp/tmp.just_random_name.png /tmp/tmp.just_random_name --oem 1  -l eng

microdict "$(cat /tmp/tmp.just_random_name.txt)"

rm /tmp/tmp.just_random_name.png
rm /tmp/tmp.just_random_name.txt
```

### Usage Steps

#### Step 1: Install Required Software (Dependencies)

Depending on your system, you need to install the following tools:

- **OCR Engine**: tesseract and its language packs.
- **Screenshot Tool**: Choose according to your desktop environment:
    - KDE (Plasma): Install spectacle.
    - Sway (Wayland): Install grim and slurp.
- **Other**: bash environment.

Example installation for Ubuntu/Debian:

```bash
sudo apt update
sudo apt install tesseract-ocr tesseract-ocr-eng tesseract-ocr-chi-sim  spectacle
```

> Note: `chi-sim` is the Chinese language pack. The script example only uses English `eng`, but it's recommended to include Chinese as well.

#### Step 2: Create the Script File

Create a new file in your home directory, for example `ocr_translate.sh`:

```bash
nano ~/ocr_translate.sh
```

Copy the code above into it (or type it manually).

Save and exit (In Nano, press `Ctrl+O`, then Enter, then `Ctrl+X`).

#### Step 3: Make It Executable

Run the following command in terminal to make the script executable:

```bash
chmod +x ~/ocr_translate.sh
```

#### Step 4: Use the Script

1. Start MicroDict first (let it run in the background).
2. Execute the script:

```bash
./ocr_translate.sh
```

**Operation**:

If you're using KDE, your mouse will turn into a crosshair. Drag to select the text region you want to translate.
The script will automatically recognize the text, and the running MicroDict will look it up in its main window.

### 💡 Advanced Optimization Tips

#### Set Up a Keyboard Shortcut

This is the most recommended way to use it. In your system settings -> Shortcuts, add a global shortcut (such as `Ctrl + Alt + G`) pointing to the absolute path of this script (e.g., `/home/your_username/ocr_translate.sh`). This way, whenever you see an unknown word, just press the shortcut to capture it.

#### Support Chinese Recognition

If you need to recognize Chinese, modify the `tesseract` line in the script:

Change `-l eng` to `-l eng+chi_sim` (requires the Chinese language pack to be installed).

#### For GNOME Users

If you're using GNOME desktop, the `case` in the script won't match. You can add a GNOME screenshot command, or use the more generic:

```bash
gnome-screenshot -a -f /tmp/tmp.just_random_name.png
```
