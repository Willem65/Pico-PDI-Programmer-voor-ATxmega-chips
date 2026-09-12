import json
import os
import shutil
import subprocess
import tkinter as tk
from tkinter import filedialog, messagebox

CONFIG_FILE = "avrdude_config.json"


class AvrdudeGUI:

  def __init__(self, root):
    self.root = root
    self.root.title("AVRDUDE GUI Wrapper (XMEGA)")
    self.root.geometry("540x600")
    self.root.minsize(500, 560)

    # --- Variabelen ---
    self.use_sudo_var = tk.BooleanVar(value=True)
    self.remember_pwd_var = tk.BooleanVar(value=False)
    self.password_var = tk.StringVar(value="")

    self.programmer_var = tk.StringVar(value="jtag2pdi")
    self.part_var = tk.StringVar(value="atxmega128a4u")
    self.port_var = tk.StringVar(value="/dev/ttyACM0")
    self.baud_var = tk.StringVar(value="19200")
    self.memtype_var = tk.StringVar(value="flash")
    self.op_var = tk.StringVar(value="r")  # r = read, w = write
    self.filename_var = tk.StringVar(value="firmware_backup.bin")

    # Laad configuratie
    self.load_config()

    self.create_widgets()

  def create_widgets(self):
    pad = {"padx": 5, "pady": 5}

    # Frame: Sudo & Configuratie
    frame_config = tk.LabelFrame(
        self.root, text=" Sudo & AVRDUDE Configuratie ", font=("Arial", 10, "bold")
    )
    frame_config.pack(fill="x", padx=10, pady=10)

    # Gebruik sudo
    tk.Checkbutton(
        frame_config,
        text="Gebruik 'sudo' (voor poort-toegang)",
        variable=self.use_sudo_var,
    ).grid(row=0, column=0, columnspan=2, sticky="w", **pad)

    # Wachtwoordveld
    tk.Label(frame_config, text="Sudo Wachtwoord:").grid(
        row=1, column=0, sticky="e", **pad
    )
    tk.Entry(
        frame_config, textvariable=self.password_var, show="*", width=25
    ).grid(row=1, column=1, sticky="w", **pad)

    # Onthoud wachtwoord checkbox
    tk.Checkbutton(
        frame_config,
        text="Onthoud wachtwoord",
        variable=self.remember_pwd_var,
    ).grid(row=2, column=0, columnspan=2, sticky="w", **pad)

    # Programmer (-c)
    tk.Label(frame_config, text="Programmer (-c):").grid(
        row=3, column=0, sticky="e", **pad
    )
    tk.Entry(
        frame_config, textvariable=self.programmer_var, width=25
    ).grid(row=3, column=1, sticky="w", **pad)

    # MCU / Chip (-p) - Dropdown
    tk.Label(frame_config, text="Microcontroller (-p):").grid(
        row=4, column=0, sticky="e", **pad
    )
    xmega_parts = [
        "atxmega128a4u",
        "atxmega64a4u",
        "atxmega32a4u",
        "atxmega16a4u",
        "atxmega128a1",
        "atxmega64a1",
        "atxmega256a3bu",
        "atxmega128a3u",
        "atxmega32e5",
        "atxmega384c3",
    ]
    part_dropdown = tk.OptionMenu(frame_config, self.part_var, *xmega_parts)
    part_dropdown.config(width=22)
    part_dropdown.grid(row=4, column=1, sticky="w", **pad)

    # Port (-P)
    tk.Label(frame_config, text="Poort (-P):").grid(
        row=5, column=0, sticky="e", **pad
    )
    tk.Entry(frame_config, textvariable=self.port_var, width=25).grid(
        row=5, column=1, sticky="w", **pad
    )

    # Baudrate (-b) - Alleen-lezen
    tk.Label(frame_config, text="Baudrate (-b):").grid(
        row=6, column=0, sticky="e", **pad
    )
    tk.Entry(
        frame_config,
        textvariable=self.baud_var,
        width=25,
        state="readonly",
        readonlybackground="#e9ecef",
    ).grid(row=6, column=1, sticky="w", **pad)

    # Actie & Geheugentype & Bestandsnaam
    frame_op = tk.LabelFrame(
        self.root, text=" Operatie (-U optie)", font=("Arial", 10, "bold")
    )
    frame_op.pack(fill="x", padx=10, pady=5)

    # Geheugentype (Dropdown)
    tk.Label(frame_op, text="Geheugentype:").grid(
        row=0, column=0, sticky="e", **pad
    )
    mem_choices = [
        "flash",
        "eeprom",
        "fuse",
        "lockbits",
        "usersig",
        "signature",
    ]
    mem_dropdown = tk.OptionMenu(
        frame_op,
        self.memtype_var,
        *mem_choices,
        command=self.update_filename_on_switch,
    )
    mem_dropdown.config(width=15)
    mem_dropdown.grid(row=0, column=1, sticky="w", **pad)

    # Actie (Lezen / Schrijven)
    tk.Label(frame_op, text="Actie:").grid(row=1, column=0, sticky="e", **pad)
    op_frame = tk.Frame(frame_op)
    op_frame.grid(row=1, column=1, columnspan=2, sticky="w", **pad)
    tk.Radiobutton(
        op_frame, text="Lezen (r)", variable=self.op_var, value="r"
    ).pack(side="left")
    tk.Radiobutton(
        op_frame, text="Schrijven (w)", variable=self.op_var, value="w"
    ).pack(side="left")

    # Bestandsnaam + Bladeren Knop
    tk.Label(frame_op, text="Bestand:").grid(row=2, column=0, sticky="e", **pad)
    tk.Entry(frame_op, textvariable=self.filename_var, width=25).grid(
        row=2, column=1, sticky="w", **pad
    )
    tk.Button(
        frame_op, text="Bladeren...", command=self.browse_file, bg="#e0e0e0"
    ).grid(row=2, column=2, sticky="w", **pad)

    # Actieknop
    btn_frame = tk.Frame(self.root)
    btn_frame.pack(fill="x", padx=10, pady=10)

    self.run_button = tk.Button(
        btn_frame,
        text="Start in Actieve Terminal",
        bg="green",
        fg="white",
        activebackground="lightblue",
        activeforeground="black",
        font=("Arial", 11, "bold"),
        command=self.run_in_terminal,
    )
    self.run_button.pack(fill="x", padx=5, ipady=5)

  def browse_file(self):
    if self.op_var.get() == "w":
      filename = filedialog.askopenfilename(
          title="Selecteer te programmeren bestand",
          filetypes=[
              ("Binary / Hex bestanden", "*.bin *.hex"),
              ("Alle bestanden", "*.*"),
          ],
      )
    else:
      filename = filedialog.asksaveasfilename(
          title="Bestand opslaan als...",
          filetypes=[
              ("Binary bestanden", "*.bin"),
              ("Alle bestanden", "*.*"),
          ],
          defaultextension=".bin",
      )

    if filename:
      self.filename_var.set(filename)

  def load_config(self):
    if os.path.exists(CONFIG_FILE):
      try:
        with open(CONFIG_FILE, "r") as f:
          data = json.load(f)
          self.programmer_var.set(data.get("programmer", "jtag2pdi"))
          self.port_var.set(data.get("port", "/dev/ttyACM0"))
          self.baud_var.set(data.get("baud", "19200"))
          self.use_sudo_var.set(data.get("use_sudo", True))

          self.part_var.set(data.get("part", "atxmega128a4u"))
          self.memtype_var.set(data.get("memtype", "flash"))
          self.op_var.set(data.get("op", "r"))
          self.filename_var.set(data.get("filename", "firmware_backup.bin"))

          remember = data.get("remember_pwd", False)
          self.remember_pwd_var.set(remember)
          if remember:
            self.password_var.set(data.get("password", ""))
      except Exception as e:
        print(f"Fout bij laden configuratie: {e}")

  def save_config(self):
    data = {
        "programmer": self.programmer_var.get(),
        "part": self.part_var.get(),
        "port": self.port_var.get(),
        "baud": self.baud_var.get(),
        "use_sudo": self.use_sudo_var.get(),
        "memtype": self.memtype_var.get(),
        "op": self.op_var.get(),
        "filename": self.filename_var.get(),
        "remember_pwd": self.remember_pwd_var.get(),
        "password": self.password_var.get()
        if self.remember_pwd_var.get()
        else "",
    }
    try:
      with open(CONFIG_FILE, "w") as f:
        json.dump(data, f)
    except Exception as e:
      print(f"Fout bij opslaan configuratie: {e}")

  def update_filename_on_switch(self, *args):
    mtype = self.memtype_var.get()
    if mtype == "flash":
      self.filename_var.set("firmware_backup.bin")
    elif mtype == "eeprom":
      self.filename_var.set("eeprom_backup.bin")
    elif mtype == "fuse":
      self.filename_var.set("fuses.bin")
    elif mtype == "lockbits":
      self.filename_var.set("lockbits.bin")
    elif mtype == "usersig":
      self.filename_var.set("usersig.bin")
    elif mtype == "signature":
      self.filename_var.set("signature.bin")

  def run_in_terminal(self):
    self.save_config()

    cmd_parts = []
    pwd = self.password_var.get()

    if self.use_sudo_var.get():
      if pwd:
        cmd_parts.append(f"echo '{pwd}' | sudo -S")
      else:
        cmd_parts.append("sudo")

    cmd_parts.extend([
        "avrdude",
        "-c",
        self.programmer_var.get(),
        "-p",
        self.part_var.get(),
        "-P",
        self.port_var.get(),
        "-b",
        self.baud_var.get(),
        "-U",
        f"{self.memtype_var.get()}:{self.op_var.get()}:{self.filename_var.get()}",
    ])

    full_cmd = " ".join(cmd_parts)
    wrapped_cmd = f"{full_cmd}; echo ''; echo '----------------------------------------'; read -p 'Klaar! Druk op Enter om dit venster te sluiten...'"

    terminal_emulator = None
    for term in ["gnome-terminal", "xfce4-terminal", "konsole", "xterm"]:
      if shutil.which(term):
        terminal_emulator = term
        break

    try:
      if terminal_emulator == "gnome-terminal":
        subprocess.Popen([terminal_emulator, "--", "bash", "-c", wrapped_cmd])
      elif terminal_emulator == "xterm":
        subprocess.Popen([terminal_emulator, "-e", wrapped_cmd])
      elif terminal_emulator == "xfce4-terminal":
        subprocess.Popen(
            [terminal_emulator, "-e", f"bash -c '{wrapped_cmd}'"]
        )
      elif terminal_emulator == "konsole":
        subprocess.Popen([terminal_emulator, "-e", "bash", "-c", wrapped_cmd])
      else:
        messagebox.showerror(
            "Fout",
            "Geen ondersteunde terminal-emulator gevonden!",
        )
    except Exception as e:
      messagebox.showerror(
          "Fout", f"Kon het terminalvenster niet openen: {e}"
      )


if __name__ == "__main__":
  root = tk.Tk()
  app = AvrdudeGUI(root)
  root.mainloop()