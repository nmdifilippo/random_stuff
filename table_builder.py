#!/usr/bin/env python3
"""table_builder.py : a small GUI that builds a PostgreSQL/PostGIS CREATE TABLE command.

Pick a table name, add the columns you want (name, type, PK / NOT NULL / UNIQUE, default),
and the SQL updates live. Then copy it, save it as a .sql file, or run it on your database.

Requires: python3 with tkinter       (sudo apt install python3-tk)
Optional: psql on the PATH to use the "Run on database" button
          (sudo apt install postgresql-client)
"""
import re
import subprocess
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

TYPES = [
    "TEXT", "INTEGER", "BIGINT", "SMALLINT", "SERIAL", "BIGSERIAL",
    "DOUBLE PRECISION", "NUMERIC", "BOOLEAN", "DATE", "TIMESTAMPTZ", "TIMESTAMP",
    "UUID", "JSONB",
    "GEOGRAPHY(Point,4326)", "GEOGRAPHY(LineString,4326)", "GEOGRAPHY(Polygon,4326)",
    "GEOMETRY(Point,4326)", "GEOMETRY(LineString,4326)", "GEOMETRY(Polygon,4326)",
]

# (name, type, primary_key, not_null, unique, default)
PRESETS = {
    "Contacts": ("contacts", [
        ("id", "BIGSERIAL", True, False, False, ""),
        ("name", "TEXT", False, True, False, ""),
        ("email", "TEXT", False, False, False, ""),
        ("phone", "TEXT", False, False, False, ""),
        ("notes", "TEXT", False, False, False, ""),
        ("created_at", "TIMESTAMPTZ", False, True, False, "now()"),
    ]),
    "Navigation points": ("nav_points", [
        ("id", "BIGSERIAL", True, False, False, ""),
        ("name", "TEXT", False, True, False, ""),
        ("category", "TEXT", False, True, False, "'waypoint'"),
        ("geom", "GEOGRAPHY(Point,4326)", False, True, False, ""),
        ("elevation_m", "DOUBLE PRECISION", False, False, False, ""),
        ("heading_deg", "DOUBLE PRECISION", False, False, False, ""),
        ("speed_mps", "DOUBLE PRECISION", False, False, False, ""),
        ("recorded_at", "TIMESTAMPTZ", False, True, False, "now()"),
    ]),
}


def is_spatial(type_name):
    return type_name.strip().upper().startswith(("GEOGRAPHY", "GEOMETRY"))


def build_sql(table, columns, if_not_exists=True, add_extension=True, add_gist=True):
    """Return the SQL text. `columns` is a list of (name, type, pk, not_null, unique, default).

    Problems are returned as a line starting with '--' (a SQL comment).
    """
    table = table.strip()
    if not IDENT.match(table):
        return "-- Enter a valid table name (letters, digits, underscore; cannot start with a digit)"

    cols = [c for c in columns if c[0].strip()]
    if not cols:
        return "-- Add at least one column"

    seen = set()
    for name, typ, *_ in cols:
        name = name.strip()
        if not IDENT.match(name):
            return f"-- Invalid column name: {name!r}"
        if name.lower() in seen:
            return f"-- Duplicate column name: {name!r}"
        seen.add(name.lower())
        if not typ.strip():
            return f"-- Choose a type for column {name!r}"

    pk_cols = [c[0].strip() for c in cols if c[2]]
    lines = []
    for name, typ, pk, not_null, unique, default in cols:
        parts = [name.strip(), typ.strip()]
        if pk and len(pk_cols) == 1:
            parts.append("PRIMARY KEY")          # implies NOT NULL
        elif not_null:
            parts.append("NOT NULL")
        if unique and not (pk and len(pk_cols) == 1):
            parts.append("UNIQUE")
        if default.strip():
            parts.append("DEFAULT " + default.strip())
        lines.append(" ".join(parts))
    if len(pk_cols) > 1:
        lines.append("PRIMARY KEY (" + ", ".join(pk_cols) + ")")

    ine = "IF NOT EXISTS " if if_not_exists else ""
    spatial = [c[0].strip() for c in cols if is_spatial(c[1])]

    out = []
    if add_extension and spatial:
        out.append("CREATE EXTENSION IF NOT EXISTS postgis;\n")
    out.append(f"CREATE TABLE {ine}{table} (\n  " + ",\n  ".join(lines) + "\n);")
    if add_gist:
        for col in spatial:
            out.append(f"CREATE INDEX {ine}{table}_{col}_idx ON {table} USING GIST ({col});")
    return "\n".join(out)


class ColumnRow:
    def __init__(self, app, parent, data=("", "TEXT", False, False, False, "")):
        name, typ, pk, nn, uq, default = data
        self.name = tk.StringVar(value=name)
        self.type = tk.StringVar(value=typ)
        self.pk = tk.BooleanVar(value=pk)
        self.nn = tk.BooleanVar(value=nn)
        self.uq = tk.BooleanVar(value=uq)
        self.default = tk.StringVar(value=default)

        self.frame = ttk.Frame(parent)
        ttk.Entry(self.frame, textvariable=self.name, width=20).grid(row=0, column=0, padx=2, pady=1)
        ttk.Combobox(self.frame, textvariable=self.type, values=TYPES, width=27).grid(row=0, column=1, padx=2)
        ttk.Checkbutton(self.frame, variable=self.pk).grid(row=0, column=2, padx=14)
        ttk.Checkbutton(self.frame, variable=self.nn).grid(row=0, column=3, padx=14)
        ttk.Checkbutton(self.frame, variable=self.uq).grid(row=0, column=4, padx=14)
        ttk.Entry(self.frame, textvariable=self.default, width=18).grid(row=0, column=5, padx=2)
        ttk.Button(self.frame, text="\u2715", width=3,
                   command=lambda: app.remove_row(self)).grid(row=0, column=6, padx=2)
        self.frame.pack(anchor="w")

        for v in (self.name, self.type, self.pk, self.nn, self.uq, self.default):
            v.trace_add("write", lambda *_: app.refresh())

    def values(self):
        return (self.name.get(), self.type.get(), self.pk.get(),
                self.nn.get(), self.uq.get(), self.default.get())


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("PostGIS table builder")
        self.geometry("820x720")
        self.rows = []

        # --- table options ---
        top = ttk.Frame(self, padding=8)
        top.pack(fill="x")
        ttk.Label(top, text="Table name:").pack(side="left")
        self.table = tk.StringVar(value="my_table")
        ttk.Entry(top, textvariable=self.table, width=24).pack(side="left", padx=6)
        self.ine = tk.BooleanVar(value=True)
        self.ext = tk.BooleanVar(value=True)
        self.gist = tk.BooleanVar(value=True)
        ttk.Checkbutton(top, text="IF NOT EXISTS", variable=self.ine).pack(side="left", padx=6)
        ttk.Checkbutton(top, text="Enable PostGIS", variable=self.ext).pack(side="left", padx=6)
        ttk.Checkbutton(top, text="Spatial index", variable=self.gist).pack(side="left", padx=6)
        mb = ttk.Menubutton(top, text="Load example")
        menu = tk.Menu(mb, tearoff=0)
        for label in PRESETS:
            menu.add_command(label=label, command=lambda l=label: self.load_preset(l))
        mb["menu"] = menu
        mb.pack(side="right")
        for v in (self.table, self.ine, self.ext, self.gist):
            v.trace_add("write", lambda *_: self.refresh())

        # --- column header + scrollable rows ---
        header = ttk.Frame(self, padding=(8, 0))
        header.pack(fill="x")
        for text, w in (("Column name", 22), ("Type (editable)", 30), ("PK", 6),
                        ("Not null", 8), ("Unique", 7), ("Default (SQL)", 18)):
            ttk.Label(header, text=text, width=w, anchor="w").pack(side="left")

        mid = ttk.Frame(self, padding=(8, 0))
        mid.pack(fill="both", expand=True)
        canvas = tk.Canvas(mid, height=230, highlightthickness=0)
        scroll = ttk.Scrollbar(mid, orient="vertical", command=canvas.yview)
        self.rows_frame = ttk.Frame(canvas)
        self.rows_frame.bind("<Configure>",
                             lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=self.rows_frame, anchor="nw")
        canvas.configure(yscrollcommand=scroll.set)
        canvas.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")

        ttk.Button(self, text="+ Add column", command=lambda: self.add_row()).pack(anchor="w", padx=8, pady=4)

        # --- SQL preview ---
        ttk.Label(self, text="Generated SQL:", padding=(8, 0)).pack(anchor="w")
        self.preview = tk.Text(self, height=12, font=("monospace", 10), state="disabled", wrap="none")
        self.preview.pack(fill="both", expand=True, padx=8, pady=4)

        # --- actions ---
        bar = ttk.Frame(self, padding=8)
        bar.pack(fill="x")
        ttk.Button(bar, text="Copy SQL", command=self.copy).pack(side="left")
        ttk.Button(bar, text="Save .sql\u2026", command=self.save).pack(side="left", padx=6)
        ttk.Label(bar, text="  psql connection:").pack(side="left")
        self.conn = tk.StringVar(value="dbname=geodb")
        ttk.Entry(bar, textvariable=self.conn, width=30).pack(side="left", padx=6)
        ttk.Button(bar, text="Run on database", command=self.run).pack(side="left")

        self.load_preset("Contacts")

    # ----- row management -----
    def add_row(self, data=("", "TEXT", False, False, False, "")):
        self.rows.append(ColumnRow(self, self.rows_frame, data))
        self.refresh()

    def remove_row(self, row):
        row.frame.destroy()
        self.rows.remove(row)
        self.refresh()

    def load_preset(self, label):
        table, cols = PRESETS[label]
        for r in self.rows:
            r.frame.destroy()
        self.rows.clear()
        self.table.set(table)
        for c in cols:
            self.rows.append(ColumnRow(self, self.rows_frame, c))
        self.refresh()

    # ----- SQL -----
    def sql(self):
        return build_sql(self.table.get(), [r.values() for r in self.rows],
                         self.ine.get(), self.ext.get(), self.gist.get())

    def refresh(self):
        text = self.sql()
        self.preview.configure(state="normal")
        self.preview.delete("1.0", "end")
        self.preview.insert("1.0", text)
        self.preview.configure(state="disabled")

    # ----- actions -----
    def copy(self):
        self.clipboard_clear()
        self.clipboard_append(self.sql())
        messagebox.showinfo("Copied", "SQL copied to the clipboard.")

    def save(self):
        path = filedialog.asksaveasfilename(defaultextension=".sql",
                                            filetypes=[("SQL files", "*.sql"), ("All files", "*.*")],
                                            initialfile=f"{self.table.get().strip() or 'table'}.sql")
        if path:
            with open(path, "w") as f:
                f.write(self.sql() + "\n")

    def run(self):
        sql = self.sql()
        if "CREATE TABLE" not in sql:
            messagebox.showerror("Cannot run", sql.lstrip("- "))
            return
        conn = self.conn.get().strip()
        if not messagebox.askyesno("Run on database", f"Run this on '{conn}'?\n\n{sql}"):
            return
        try:
            p = subprocess.run(["psql", conn, "-v", "ON_ERROR_STOP=1", "-f", "-"],
                               input=sql, text=True, capture_output=True, timeout=30)
        except FileNotFoundError:
            messagebox.showerror("psql not found", "Install it with:\n  sudo apt install postgresql-client")
            return
        except subprocess.TimeoutExpired:
            messagebox.showerror("Timed out", "psql did not finish within 30 seconds.")
            return
        output = (p.stdout + p.stderr).strip() or "(no output)"
        (messagebox.showinfo if p.returncode == 0 else messagebox.showerror)(
            "Success" if p.returncode == 0 else "psql error", output)


if __name__ == "__main__":
    App().mainloop()