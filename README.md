# 🎮 Cemu for Android

### Your Wii U library, in your hands. On one screen, or two.

![Android 11+](https://img.shields.io/badge/Android-11%2B-3DDC84?logo=android&logoColor=white)
![64-bit ARM](https://img.shields.io/badge/CPU-64--bit%20ARM-blue)
![Vulkan](https://img.shields.io/badge/Graphics-Vulkan-AC162C?logo=vulkan&logoColor=white)
![Dual screen ready](https://img.shields.io/badge/Dual%20screen-ready-8A2BE2)
![License MPL 2.0](https://img.shields.io/badge/License-MPL%202.0-orange)

This is a community build of the **Cemu** Wii U emulator for Android. It takes the Android port and goes after everything that got in the way of just *playing*: the crashes, the fiddly setup, the "why is my controller doing nothing?" moments. Then it adds the things handheld players kept asking for.

**The result:** it's built for dual-screen handhelds like the **AYN Thor**, it's still at home on a regular phone, and it's a lot harder to break.

<!-- Screenshots go here: dual screen on the Thor, the game list, the in-game menu. -->

---

## Why this build?

| | |
|---|---|
| 🖥️ **Real dual screen** | The GamePad on your second screen, just like the Wii U. It's meant to work on the AYN Thor and other dual-screen devices. |
| 🛡️ **Built to be stable** | More than **80 bugs** tracked down and fixed, from crashes on quit to buttons that got stuck. |
| 🎮 **Controllers just work** | Connect a controller and it's mapped for you. It even remembers the layout you use with each controller model. |
| 🎨 **Sharper picture** | AMD FSR 1 upscaling, anisotropic filtering and gamma controls, built right in. |
| ⚡ **Less waiting** | Compile a game's shaders ahead of time, and no more 3-second pause every time a game starts. |
| 🧰 **Everything in the app** | GPU driver downloads, keys import, save backups, amiibo, screenshots. No file-manager detours. |

---

## ✨ The highlights

### 🖥️ Two screens, the way the Wii U was meant to be played
- **GamePad on the second screen:** the TV picture on the main screen, the GamePad (with touch) on the other.
- **Swap screens** any time. You can also **rotate** the second screen.
- **Pick your screen:** with more than one second screen (say, the Thor plus an HDMI monitor), choose which one gets the GamePad.
- **Your main screen stays smooth:** a slower second screen no longer holds back a 120 Hz main screen.
- **One screen? No problem:**
  - Show the GamePad next to the TV picture.
  - Choose its side, and how big the TV picture is.
  - Dual-screen options stay out of your way on devices that don't need them.

### 🛡️ The stability update
We went through the app piece by piece and fixed what we found:
- **Quitting is clean:** your shader progress is saved first, and there's no crash behind the scenes anymore.
- **Errors actually tell you something:** a damaged game file or a missing key now shows a message instead of the app silently vanishing.
- **Switch apps, rotate, or turn the second screen off and on** without freezes or black screens.
- **Pause means pause:**
  - The game and its audio stop.
  - In the background it stops using your battery.
- **Connecting a controller or switching dark mode mid-game** no longer restarts your game.
- **Touch controls don't stick** anymore, and pressing a button while touching the screen works.
- **Your settings stay put** after updates, instead of quietly resetting.
- **Game names with emoji or non-English characters** show up and launch properly.
- **Installing a game keeps going in the background,** and an interrupted install gets cleaned up automatically.

### 🎮 Controllers that just work
- **Plug and play:**
  - On a fresh install, controller 1 is ready to go.
  - A connected controller gets mapped automatically.
  - The built-in controls of handhelds count too.
- **"Press A" setup:** press the button you want as A, and the app works out whether your controller uses the Xbox or the Nintendo layout.
- **Layouts per controller model:** switch between your controllers, or grab a second one of the same kind, and each gets the layout you set up for that model.
- **Controller profiles:** save, load, and even pick a different profile for each game.
- **More hotkeys:** pause, screenshot, swap screens, show or hide the GamePad and the touch controls, scan an amiibo.
- **Your own touch layout for each game,** if one game needs the buttons somewhere else.
- **Motion controls follow your device** when you flip it around.

### ⚡ Smoother, faster, cooler
- **Compile shaders ahead of time:**
  - Long-press a game > **Shader cache** > **Compile now**.
  - It prepares the game's shaders for your GPU without starting the game, so you get less stutter once you play.
- **Share shader caches:** import caches from other players (or from desktop Cemu) and export your own.
- **Nothing gets lost on quit:** new shaders and pipelines found while playing are saved properly, so the next session is smoother.
- **Faster startup:** we removed a fixed 3-second wait that happened on every launch.
- **Works with Android's performance tuning:** the emulator tells Android what it needs, including when a game runs at 30 fps and doesn't need full power.
- **Sustained performance mode:** on supported devices, trade a little peak speed for steadier frame rates in long sessions.
- **Pre-rotation (experimental):** saves the phone some work rotating every frame on screens that are naturally portrait.

### 🎨 Make it look better
- **AMD FSR 1 upscaling:** crisp, sharp edges when a 720p game is stretched to your screen.
- **Anisotropic filtering, up to 16x:** roads and floors stay sharp into the distance.
- **Gamma controls:** too dark or washed out? Tune it for your screen.
- **Graphic packs for each game:** long-press a game > **Graphic packs…** to see just that game's resolution, 60 fps and visual packs.
- **GPU driver downloader:**
  - Get Turnip and other community drivers right inside the app.
  - It recommends one for your Adreno GPU.

### 🧰 All the little things
- **Import your keys file** from anywhere, and new games show up without a restart.
- **Import otp.bin and seeprom.bin** for online features.
- **Back up and restore saves** for any game as a simple zip.
- **Keep your saves in a folder you choose,** mirrored automatically.
- **Scan amiibo** straight from the in-game menu.
- **Screenshots** land in your gallery (Pictures/Cemu).
- **A performance overlay** that shows battery, temperature, throttling and a frame-time graph, next to FPS, CPU and RAM.
- **Home-screen shortcuts** to jump straight into a game.

---

## Before & after

| | Original Android port | This build |
|---|:---:|:---:|
| GamePad on a second screen | ❌ | ✅ |
| Choose which second screen | ❌ | ✅ |
| Shader progress saved when you quit | ❌ | ✅ |
| Error messages instead of silent closes | ❌ | ✅ |
| Automatic controller setup | ❌ | ✅ |
| Layouts remembered per controller model | ❌ | ✅ |
| GPU driver downloads in the app | ❌ | ✅ |
| Compile / import / export shader caches | ❌ | ✅ |
| AMD FSR 1, anisotropic filtering, gamma | ❌ | ✅ |
| Pause, screenshot and amiibo from the menu | ❌ | ✅ |
| Save backup & restore | ❌ | ✅ |
| Battery, temperature & frame-time overlay | ❌ | ✅ |

---

## 🚀 Get started

**You'll need:**
- Android 11 or newer.
- A 64-bit ARM device with Vulkan (most phones and handhelds from the last few years).
- Your own Wii U games and keys. Nothing is included.

**In five steps:**
1. Install the APK from the **Releases** page.
2. Open **Settings > General > Add game path** and pick the folder with your games.
3. Missing a key? **Settings > Import keys file** and choose your `keys.txt`.
4. Connect a controller if you like. It's set up for you.
5. Tap a game and play. 🎉

**Tips for the best experience:**
- 🥇 First time playing a game? Long-press it > **Shader cache > Compile now** before you start.
- 📱 On a phone with an Adreno GPU, try a driver from **Settings > Graphics > Custom drivers**.
- 🖥️ On a dual-screen device, open the in-game menu and turn on **External PAD screen**.
- 🔍 Want a sharper picture? **Settings > Graphics > Upscale filter > AMD FSR 1**.

---

## Good to know
- This is an **independent community build** and it keeps evolving. Some features are brand new, so if something misbehaves, the game list menu has **Share log file**. Include it when you report a problem.
- Performance depends on your device and the game. Not every game runs well on every phone yet.
- Cemu does not come with any games, keys or system files. Please use dumps of games you own.
- **On our radar:** save states. We're looking into it.

---

## ❤️ Credits
- **[Cemu](https://github.com/cemu-project/Cemu)** by the Cemu team, the Wii U emulator this all runs on ([cemu.info](https://cemu.info)).
- **[SSimco's Android port](https://github.com/SSimco/Cemu)**, which brought Cemu to Android.
- **AMD FidelityFX Super Resolution 1** (MIT license) for the FSR upscaler.

Not affiliated with Nintendo. Wii U is a trademark of Nintendo.

## License
Cemu is licensed under [Mozilla Public License 2.0](/LICENSE.txt). Exempt from this are all files in the dependencies directory for which the licenses of the original code apply as well as some individual files in the src folder, as specified in those file headers respectively.
