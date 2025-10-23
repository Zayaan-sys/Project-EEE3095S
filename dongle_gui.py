import serial
import serial.tools.list_ports
import time
import threading
import pyperclip
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog

# ========== GUI STYLING ==========
APP_TITLE = "Dongle Lock"
FONT_MAIN = ("Segoe UI", 11)
FONT_TITLE = ("Segoe UI Semibold", 16)
PADDING = 20

# Color scheme
BG_COLOR = "#2c3e50"  # Dark blue-gray background
FG_COLOR = "#ecf0f1"  # Light text
ACCENT_COLOR = "#3498db"  # Bright blue accent
BUTTON_BG = "#34495e"  # Button background
BUTTON_FG = "#ecf0f1"  # Button text

# ========== CUSTOM PASSWORD DIALOG ==========
class PasswordDialog(simpledialog.Dialog):
    def __init__(self, parent, title, prompt):
        self.prompt = prompt
        self.password = None
        super().__init__(parent, title)
    
    def body(self, master):
        ttk.Label(master, text=self.prompt, font=FONT_MAIN).grid(row=0, sticky="w", pady=(0, 10))
        self.entry = ttk.Entry(master, show="*", width=30, font=FONT_MAIN)
        self.entry.grid(row=1, sticky="ew")
        return self.entry
    
    def apply(self):
        self.password = self.entry.get()

# ========== MAIN APP ==========
class DongleApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("500x420")
        self.resizable(False, False)
        
        # Set window background color
        self.configure(bg=BG_COLOR)

        # Configure ttk styles
        self.style = ttk.Style(self)
        self.style.theme_use("clam")
        
        # Configure label style
        self.style.configure("Title.TLabel", 
                           background=BG_COLOR, 
                           foreground=FG_COLOR, 
                           font=FONT_TITLE)
        self.style.configure("TLabel", 
                           background=BG_COLOR, 
                           foreground=FG_COLOR, 
                           font=FONT_MAIN)
        self.style.configure("Accent.TLabel", 
                           background=BG_COLOR, 
                           foreground=ACCENT_COLOR, 
                           font=FONT_MAIN)
        
        # Configure button style
        self.style.configure("TButton",
                           background=BUTTON_BG,
                           foreground=BUTTON_FG,
                           borderwidth=0,
                           focuscolor="none",
                           font=FONT_MAIN,
                           padding=10)
        self.style.map("TButton",
                     background=[("active", ACCENT_COLOR), ("pressed", "#2980b9")])
        
        # Configure combobox style
        self.style.configure("TCombobox",
                           fieldbackground=BUTTON_BG,
                           background=BUTTON_BG,
                           foreground=FG_COLOR,
                           arrowcolor=FG_COLOR,
                           borderwidth=0)
        
        # Configure frame style
        self.style.configure("TFrame", background=BG_COLOR)

        # Serial attributes
        self.serial_connection = None
        self.connected_port = tk.StringVar()
        self.status_text = tk.StringVar(value="Not connected")

        self._build_connect_ui()

    # ========== UI BUILDERS ==========
    def _build_connect_ui(self):
        """Initial connection frame."""
        for w in self.winfo_children():
            w.destroy()

        frame = ttk.Frame(self, padding=PADDING)
        frame.pack(expand=True)

        ttk.Label(frame, text="Dongle Lock Interface", style="Title.TLabel").pack(pady=(0, 20))

        # Port selection
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if not ports:
            ports = ["No Ports Found"]
        ttk.Label(frame, text="Select Serial Port:").pack(pady=(0, 5))
        port_box = ttk.Combobox(frame, textvariable=self.connected_port, values=ports, state="readonly", width=30)
        port_box.pack(pady=(0, 15))
        if ports:
            port_box.current(0)

        ttk.Button(frame, text="🔄 Refresh Ports", command=self._refresh_ports).pack(pady=(0, 8))
        ttk.Button(frame, text="Connect", command=self._connect_dongle).pack(pady=(0, 20))

        ttk.Label(frame, textvariable=self.status_text, style="Accent.TLabel").pack(pady=(10, 0))

    def _build_main_ui(self):
        """Main menu after successful connection."""
        for w in self.winfo_children():
            w.destroy()

        frame = ttk.Frame(self, padding=PADDING)
        frame.pack(expand=True, fill="both")

        ttk.Label(frame, text="Dongle Connected", style="Title.TLabel").pack(pady=(0, 10))
        ttk.Label(frame, text=f"Port: {self.connected_port.get()}", style="Accent.TLabel").pack(pady=(0, 20))

        # Button grid
        for i in range(1, 4):
            ttk.Button(frame, text=f"🔒 Get Code {i}", width=25,
                       command=lambda n=i: self._get_code(n)).pack(pady=6)

        ttk.Button(frame, text="🧹 Clear All Codes", width=25, command=self._clear_all).pack(pady=(20, 6))
        ttk.Button(frame, text="Exit", width=25, command=self._disconnect).pack(pady=6)

        ttk.Label(frame, textvariable=self.status_text, style="Accent.TLabel").pack(pady=(20, 0))

    # ========== FUNCTIONAL LOGIC ==========
    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if not ports:
            messagebox.showwarning("No Ports", "No serial ports detected.")
        else:
            self.connected_port.set(ports[0])
        self._build_connect_ui()

    def _connect_dongle(self):
        port = self.connected_port.get()
        if not port or "No Ports" in port:
            messagebox.showerror("Connection Error", "No valid serial port selected.")
            return

        try:
            self.serial_connection = serial.Serial(port, 115200, timeout=2)
            time.sleep(2)  # allow MCU to boot
            self._send("CONNECT")
            reply = self._recv()
            if reply == "OK":
                self.status_text.set(f"Connected to {port}")
                self._build_main_ui()
            else:
                messagebox.showerror("Device Not Found", f"Dongle not responding on {port}")
                self.serial_connection.close()
                self.serial_connection = None
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def _disconnect(self):
        if self.serial_connection:
            try:
                self._send("DISCONNECT")
                self._recv()
                self.serial_connection.close()
            except Exception:
                pass
        self.serial_connection = None
        self.status_text.set("Disconnected")
        pyperclip.copy("")  # clear clipboard
        self._build_connect_ui()

    def _send(self, line):
        if self.serial_connection and self.serial_connection.is_open:
            try:
                self.serial_connection.write((line + "\n").encode("utf-8"))
                self.serial_connection.flush()
            except serial.SerialException:
                # Connection lost during send
                return False
        return True

    def _recv(self):
        if self.serial_connection:
            try:
                reply = self.serial_connection.readline().decode("utf-8", errors="ignore").strip()
                return reply if reply else ""
            except serial.SerialException:
                # Connection lost during receive
                return ""
        return ""

    def _get_code(self, n):
        self._send(f"GET_CODE_{n}")
        reply = self._recv()
        
        # Check if device disconnected (empty reply or timeout)
        if reply == "" or reply == "TIMEOUT":
            messagebox.showerror("Device Disconnected", 
                               "Dongle disconnected. Please reconnect the device.")
            self._disconnect()  # Return to connection screen
            return
        
        if reply.startswith("CODE:"):
            code = reply.split(":", 1)[1]
            pyperclip.copy(code)
            # Show message without revealing the actual code
            messagebox.showinfo("Code Copied", f"Code {n} copied to clipboard\n(Password hidden for security)")
        elif reply == "NO_CODE":
            # Use custom password dialog with masked input
            dialog = PasswordDialog(self, "Set New Code", f"Enter new code for slot {n}:")
            new_code = dialog.password
            
            if new_code:
                self._send(f"SET_CODE_{n}:{new_code}")
                reply = self._recv()
                
                # Check for disconnection during save
                if reply == "" or reply == "TIMEOUT":
                    messagebox.showerror("Device Disconnected", 
                                       "Dongle disconnected during save. Please reconnect.")
                    self._disconnect()
                    return
                
                if reply == "SAVED":
                    pyperclip.copy(new_code)
                    messagebox.showinfo("Code Saved", f"Code {n} saved and copied to clipboard\n(Password hidden for security)")
                else:
                    messagebox.showerror("Error", "Failed to save code.")
        else:
            messagebox.showwarning("Unexpected", f"Unexpected reply: {reply}")

    def _clear_all(self):
        if not messagebox.askyesno("Confirm", "Clear all stored codes?"):
            return
        self._send("CLEAR_ALL")
        reply = self._recv()
        
        # Check for disconnection
        if reply == "" or reply == "TIMEOUT":
            messagebox.showerror("Device Disconnected", 
                               "Dongle disconnected. Please reconnect the device.")
            self._disconnect()
            return
        
        if reply == "CLEARED":
            messagebox.showinfo("Cleared", "All codes cleared.")
        else:
            messagebox.showerror("Error", f"Unexpected reply: {reply}")

# ========== MAIN ==========
if __name__ == "__main__":
    app = DongleApp()
    app.mainloop()