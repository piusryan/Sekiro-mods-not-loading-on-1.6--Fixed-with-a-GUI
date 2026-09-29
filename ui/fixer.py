"""Mod Engine 1.6 Fixer — a small GUI for people who should not have to read
source code to install a mod loader.

Four steps, top to bottom:
  1. Point it at your game folder
  2. Build the patched DLL        (only needed once)
  3. Install it                   (backs up whatever was there)
  4. Play, then check the report
"""

from __future__ import annotations

import os
import queue
import subprocess
import sys
import threading
import traceback
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

# Plain import: works when run as `python ui\fixer.py` (sys.path[0] is ui\),
# when imported as part of the package, and when frozen by PyInstaller.
from modengine_fixer import core

APP_TITLE = "Mod Engine 1.6 Fixer"
REPO_ROOT = core.REPO_ROOT


class Tooltip:
    """Cheap tooltip: attach to any widget."""

    def __init__(self, widget, text: str):
        self.widget = widget
        self.text = text
        self.tip: tk.Toplevel | None = None
        widget.bind("<Enter>", self._show, add="+")
        widget.bind("<Leave>", self._hide, add="+")

    def _show(self, _event=None):
        if self.tip or not self.text:
            return
        x = self.widget.winfo_rootx() + 12
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.tip = tk.Toplevel(self.widget)
        self.tip.wm_overrideredirect(True)
        self.tip.wm_geometry(f"+{x}+{y}")
        tk.Label(
            self.tip, text=self.text, justify="left", wraplength=340,
            background="#ffffe0", relief="solid", borderwidth=1,
            padx=6, pady=4,
        ).pack()

    def _hide(self, _event=None):
        if self.tip:
            self.tip.destroy()
            self.tip = None


def detect_game_dir() -> Path | None:
    """Best-effort guess at the game folder, so the UI is not empty on open."""
    candidates: list[Path] = []
    steam = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) \
        / "Steam/steamapps/common/Sekiro"
    candidates.append(steam)
    here = Path.cwd()
    candidates.extend([here, here / "Sekiro", REPO_ROOT])

    for candidate in candidates:
        if (candidate / "sekiro.exe").is_file():
            return candidate.resolve()

    # Common library locations
    for pf in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles")):
        if not pf:
            continue
        root = Path(pf) / "SteamLibrary/steamapps/common"
        if root.is_dir():
            for entry in root.iterdir():
                if (entry / "sekiro.exe").is_file():
                    return entry.resolve()
    return None


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.minsize(760, 640)
        self.configure(padx=12, pady=10)

        guess = detect_game_dir()
        self.game_dir: Path = guess or REPO_ROOT
        self.status: core.GameStatus = core.GameStatus()
        self.report: core.Report = core.Report()
        self._build_queue: queue.Queue[str] = queue.Queue()
        self._busy = False

        style = ttk.Style(self)
        try:
            style.theme_use("vista")
        except tk.TclError:
            pass

        self._build_ui()
        self._build_footer()
        self.refresh()

    # ------------------------------------------------------------------ UI
    def _build_ui(self):
        notebook = ttk.Notebook(self)
        notebook.pack(fill="both", expand=True)

        # ---- Setup tab ----
        setup = ttk.Frame(notebook, padding=14)
        notebook.add(setup, text="  1. Setup  ")

        row = ttk.Frame(setup)
        row.pack(fill="x", pady=(0, 10))
        ttk.Label(row, text="Game folder:").pack(side="left")
        self.game_var = tk.StringVar(value=str(self.game_dir))
        ttk.Entry(row, textvariable=self.game_var).pack(
            side="left", fill="x", expand=True, padx=8)
        ttk.Button(row, text="Browse...", command=self.browse).pack(side="left")
        ttk.Button(row, text="Refresh", command=self.refresh).pack(side="left", padx=(6, 0))

        self.status_text = tk.Text(setup, height=16, wrap="word",
                                   font=("Consolas", 9), state="disabled")
        self.status_text.pack(fill="both", expand=True)

        btn_row = ttk.Frame(setup)
        btn_row.pack(fill="x", pady=(10, 0))

        self.build_btn = ttk.Button(btn_row, text="Build patched DLL",
                                    command=self.on_build)
        self.build_btn.pack(side="left")
        Tooltip(self.build_btn, "Compiles src\\build.cmd. Needs Visual Studio 2022 "
                                "Build Tools. Only required once.")

        self.install_btn = ttk.Button(btn_row, text="Install / repair",
                                      command=self.on_install)
        self.install_btn.pack(side="left", padx=8)
        Tooltip(self.install_btn, "Backs up any existing dinput8.dll, then copies "
                                  "the patched one in and creates modengine.ini "
                                  "if missing.")

        self.play_btn = ttk.Button(btn_row, text="Play game", command=self.on_play)
        self.play_btn.pack(side="left")
        Tooltip(self.play_btn, "Launches sekiro.exe with the correct working "
                               "directory so the mods folder is found.")

        self.build_log = tk.Text(setup, height=7, wrap="none",
                                 font=("Consolas", 8), state="disabled")
        self.build_log.pack(fill="x", pady=(10, 0))

        # ---- Report tab ----
        rep = ttk.Frame(notebook, padding=14)
        notebook.add(rep, text="  2. Mod report  ")

        head = ttk.Frame(rep)
        head.pack(fill="x", pady=(0, 8))
        ttk.Button(head, text="Refresh report", command=self.refresh_report).pack(side="left")
        self.report_summary = ttk.Label(head, text="", font=("Segoe UI", 11, "bold"))
        self.report_summary.pack(side="left", padx=12)

        self.report_text = tk.Text(rep, wrap="none", font=("Consolas", 9),
                                   state="disabled")
        self.report_text.pack(fill="both", expand=True)

        # ---- Help tab ----
        help_tab = ttk.Frame(notebook, padding=14)
        notebook.add(help_tab, text="  3. Help  ")
        help_text = tk.Text(help_tab, wrap="word", font=("Segoe UI", 10),
                            state="disabled", padx=8, pady=8)
        help_text.pack(fill="both", expand=True)
        help_text.configure(state="normal")
        help_text.insert("1.0", HELP_TEXT)
        help_text.configure(state="disabled")

    def _build_footer(self):
        footer = ttk.Frame(self)
        footer.pack(fill="x", pady=(8, 0))
        self.toolchain_var = tk.StringVar()
        ttk.Label(footer, textvariable=self.toolchain_var).pack(side="left")
        ttk.Button(footer, text="Open game folder",
                   command=lambda: os.startfile(self.game_dir)
                   ).pack(side="right")
        ttk.Button(footer, text="User guide",
                   command=lambda: os.startfile(REPO_ROOT / "docs" / "UI_GUIDE.md")
                   ).pack(side="right", padx=6)
        ttk.Button(footer, text="Docs",
                   command=lambda: os.startfile(REPO_ROOT / "docs")
                   ).pack(side="right")

    # ------------------------------------------------------------- helpers
    @staticmethod
    def _put(widget: tk.Text, text: str):
        widget.configure(state="normal")
        widget.delete("1.0", "end")
        widget.insert("1.0", text)
        widget.configure(state="disabled")

    def set_busy(self, busy: bool, message: str = ""):
        self._busy = busy
        state = "disabled" if busy else "normal"
        for btn in (self.build_btn, self.install_btn):
            btn.configure(state=state)
        if busy:
            self._put(self.build_log, message)

    # ------------------------------------------------------------- actions
    def browse(self):
        chosen = filedialog.askdirectory(title="Select the folder containing sekiro.exe")
        if chosen:
            self.game_dir = Path(chosen)
            self.game_var.set(chosen)
            self.refresh()

    def refresh(self):
        self.game_dir = Path(self.game_var.get().strip() or ".")
        self.status = core.inspect_game(self.game_dir)
        self.report = core.build_report(self.game_dir)

        lines = []
        st = self.status
        if st.problem:
            lines.append(f"[!] {st.problem}")
        if st.exe:
            lines.append(f"[+] sekiro.exe      {st.exe}")
            lines.append(f"    size            {st.exe_size:,} bytes")
            flag = "supported by stock Mod Engine" if st.version_supported \
                else "too new for stock Mod Engine (this is what we fix)"
            lines.append(f"    version         {st.version}  ->  {flag}")
        if st.dll:
            tag = "PATCHED BUILD" if st.dll_is_patched else "stock 0.1.16?"
            lines.append(f"[+] dinput8.dll     {st.dll_size:,} bytes  ({tag})")
        else:
            lines.append("[-] dinput8.dll     not installed")
        if st.backup:
            lines.append(f"[+] backup          {st.backup.name}")
        if st.ini_ok or (self.game_dir / "modengine.ini").is_file():
            mods = core.get_mods_dir(self.game_dir)
            lines.append(f"[i] mods folder    {mods}")
        lines.append("")

        # report summary inside the status tab
        rep = self.report
        if rep.hook_line:
            lines.append(f"[+] {rep.hook_line}")
        if rep.problem:
            lines.append(f"[!] {rep.problem}")
        if rep.available:
            lines.append(f"[i] archives       {len(rep.served)} of {len(rep.available)} served")
        lines.append("")
        for note in st.notes:
            lines.append(f"    {note}")
        if not st.problem and st.dll_is_patched and rep.ok and not rep.missing:
            lines.append("")
            lines.append("Looks good. Play the game and check the Mod report tab.")

        self._put(self.status_text, "\n".join(lines) or "Pick a game folder to begin.")

        ok, detail = core.toolchain_status()
        self.toolchain_var.set(f"Build tools: {detail}")

        self._refresh_report_tab()

    def _refresh_report_tab(self):
        rep = self.report
        self.report_summary.configure(
            text=f"{len(rep.served)} / {len(rep.available)} archives served")

        if not rep.available:
            body = "No .dcx / .tpf files found in the mods folder.\n\n" \
                   f"Expected in: {core.get_mods_dir(self.game_dir)}"
        else:
            lines = []
            if rep.hook_line:
                lines.append(rep.hook_line)
                lines.append("")
            lines.append(f"Available archives : {len(rep.available)}")
            lines.append(f"Served last session : {len(rep.served)}")
            lines.append("")
            if rep.served:
                lines.append("SERVED (your mods are loading)")
                lines.append("-" * 58)
                lines.extend("  + " + s for s in rep.served)
                lines.append("")
            if rep.missing:
                lines.append("NOT REQUESTED YET")
                lines.append("-" * 58)
                lines.extend("  . " + s for s in rep.missing)
                lines.append("")
                lines.append(
                    "Not a problem. The game only requests the files it needs.\n"
                    "Menu archives load as you open those screens. Archives under\n"
                    "msg\\<language>\\ only load if you select that language in\n"
                    "game options. Play further and refresh to watch it grow.")
            if not rep.missing and rep.served:
                lines.append("Every available archive was served.")
            body = "\n".join(lines)

        self._put(self.report_text, body)

    def refresh_report(self):
        self.game_dir = Path(self.game_var.get().strip() or ".")
        self.report = core.build_report(self.game_dir)
        self._refresh_report_tab()

    def on_build(self):
        def worker():
            result = core.build_dll(callback=self._build_queue.put)
            self._build_queue.put("__DONE__" if result.ok else "__FAILED__")

        self.set_busy(True, "Building... this takes 10-60 seconds the first time.\n")
        threading.Thread(target=worker, daemon=True).start()
        self._drain_build_queue()

    def _drain_build_queue(self):
        done = None
        try:
            while True:
                line = self._build_queue.get_nowait()
                if line == "__DONE__":
                    done = True
                elif line == "__FAILED__":
                    done = False
                else:
                    self.build_log.configure(state="normal")
                    self.build_log.insert("end", line + "\n")
                    self.build_log.see("end")
                    self.build_log.configure(state="disabled")
        except queue.Empty:
            pass

        if done is None:
            # keep draining until the worker signals a result
            self.after(150, self._drain_build_queue)
            return

        self.set_busy(False)
        if done:
            messagebox.showinfo(
                "Build succeeded",
                "Built dinput8_patched.dll.\n\nNow press Install / repair.")
        else:
            messagebox.showerror(
                "Build failed",
                "See the build output below.\n\n"
                "If it says the toolchain is missing, see the Help tab.")
        self.refresh()

    def on_install(self):
        self.game_dir = Path(self.game_var.get().strip() or ".")
        if not (self.game_dir / "sekiro.exe").is_file():
            messagebox.showerror(
                "Wrong folder",
                f"No sekiro.exe in:\n{self.game_dir}\n\n"
                "Pick the folder that contains it.")
            return

        dll = core.SRC_DIR / "build" / core.BUILT_DLL_NAME
        if not dll.is_file():
            if not messagebox.askyesno(
                "Not built yet",
                "The patched DLL has not been built yet.\n\n"
                "Build it now? (Needs Visual Studio Build Tools.)"):
                return
            self.on_build()
            return

        result = core.install_dll(self.game_dir, dll)
        if result.ok:
            messagebox.showinfo("Installed", result.message)
        else:
            messagebox.showerror("Install failed", result.message)
        self.refresh()

    def on_play(self):
        self.game_dir = Path(self.game_var.get().strip() or ".")
        if not (self.game_dir / "sekiro.exe").is_file():
            messagebox.showerror("Wrong folder", "No sekiro.exe here.")
            return
        try:
            core.launch_game(self.game_dir)
        except OSError as exc:
            messagebox.showerror("Could not launch", str(exc))
            return
        messagebox.showinfo(
            "Game launched",
            "Play for a few minutes, then close the game and come back to "
            "check the Mod report tab.")


HELP_TEXT = """\
WHAT THIS TOOL DOES
-------------------
Mod Engine loads loose .dcx and .tpf files from a mods\\ folder so that skin,
sword, text and loading-screen mods work. The stock 0.1.16 release silently
does nothing on Sekiro 1.6 or on non-Steam copies of the game. This tool builds
a patched version that does work, and then shows you proof that your mods
actually loaded.

The short version of why: stock Mod Engine only installs its file hooks as a
side effect of the game calling SteamAPI_Init, and on a non-Steam install that
DLL decrypts itself at load time and wipes the hook. It also refuses to run
anything newer than 1.03. Full analysis in docs/ROOT_CAUSE.md.


THE FOUR STEPS
--------------
1. GAME FOLDER
   Point this at the folder containing sekiro.exe. Usually something like
     C:\\Program Files (x86)\\Steam\\steamapps\\common\\Sekiro
   The app tries to guess it. Use Browse if it got it wrong.

2. BUILD PATCHED DLL      (only needed once)
   Compiles the source in src\\. Requires Visual Studio 2022 Build Tools with
   the C++ workload. The footer bar tells you whether it is installed; if not:

     winget install Microsoft.VisualStudio.2022.BuildTools \\
       --override "--quiet --wait --norestart \\
       --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"

3. INSTALL / REPAIR
   Backs up any existing dinput8.dll to
     dinput8.dll.modengine-0.1.16.orig.bak
   then copies the patched one in. Creates modengine.ini and the mods\\ folder
   if they are missing. Safe to run more than once.

4. PLAY, THEN CHECK THE MOD REPORT
   Press Play game. The working directory is set for you, which matters -
   Mod Engine resolves the mods folder relative to it.
   Play for a few minutes, close the game, and refresh the report.


READING THE REPORT
------------------
"Served" counts archive files the game actually asked for and got from your
mods folder. If it climbs as you play, your mods are loading.

Some files will never appear, and that is correct:
  * menu archives load as you open those screens
  * archives under msg\\<language>\\ only load for the selected language
  * character model archives load when you first meet that character


IF SOMETHING LOOKS WRONG
------------------------
  served is 0      -> the hook did not install, or the working directory is
                      wrong. Press Play game rather than a desktop shortcut.
  stock DLL warning-> you are running 0.1.16. Rebuild and reinstall.
  crash on startup -> a non-1.6-safe DLL is chained. Clear
                      chainDInput8DLLPath in modengine.ini.
  nothing happens  -> Windows may be blocking the .exe. Right-click it,
                      Properties, tick Unblock, Apply.
  app closes at once-> a fixer_crash.log appears next to the .exe with the
                      full error.

docs/UI_GUIDE.md is the full button-by-button reference.
docs/TROUBLESHOOTING.md covers all ten common cases.


A WORD OF CAUTION
-----------------
This does not touch game code. It only redirects which archive files the game
loads. That said, any mod that changes a .dcx archive can crash the game if the
file is malformed or built for a different game version. If the game starts
misbehaving, remove mods until you find the culprit.
"""


def main():
    app = App()
    app.mainloop()


def _crash(exc_type, exc, tb):
    """A windowed .exe has no console, so an unhandled exception would vanish.

    Show it in a dialog and drop a copy next to the exe.
    """
    text = "".join(traceback.format_exception(exc_type, exc, tb))
    try:
        log = core.REPO_ROOT / "fixer_crash.log"
        log.write_text(text, encoding="utf-8")
        where = f"\n\nDetails saved to:\n{log}"
    except OSError:
        where = ""
    try:
        messagebox.showerror("Mod Engine Fixer crashed", text[-1200:] + where)
    except Exception:
        pass


if __name__ == "__main__":
    sys.excepthook = _crash
    main()
