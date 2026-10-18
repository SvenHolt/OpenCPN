"""Access to the S-57 object/attribute catalogue shipped with OpenCPN.

The CSV files in ``data/s57data`` of the OpenCPN source tree are the same
files OpenCPN uses at runtime, so codes written by this tool are guaranteed to
match what the chart renderer expects.
"""

import csv
import os
import re

_HERE = os.path.dirname(os.path.abspath(__file__))
_DEFAULT_DIRS = [
    os.path.join(_HERE, "..", "..", "..", "data", "s57data"),
    "/usr/share/opencpn/s57data",
    "/usr/local/share/opencpn/s57data",
    "/usr/share/gdal",
]


def find_s57data(explicit=None):
    candidates = [explicit] if explicit else []
    if os.environ.get("SEACHART_S57DATA"):
        candidates.append(os.environ["SEACHART_S57DATA"])
    candidates += _DEFAULT_DIRS
    for d in candidates:
        if d and os.path.isfile(os.path.join(d, "s57objectclasses.csv")):
            return os.path.abspath(d)
    raise FileNotFoundError(
        "s57objectclasses.csv not found; "
        "pass --s57data <OpenCPN>/data/s57data")


def _norm(text):
    return re.sub(r"[^a-z0-9]+", " ", text.lower()).strip()


class Catalog:
    def __init__(self, directory=None):
        directory = find_s57data(directory)
        self.directory = directory
        self.objl = {}        # acronym -> code
        self.attl = {}        # acronym -> code
        self.atype = {}       # acronym -> E/L/F/I/A/S
        self.enums = {}       # acronym -> [(id, meaning)]
        self.prims = {}       # acronym -> {"Point", "Line", "Area"}
        self.allowed = {}     # acronym -> attribute acronyms of the class
        with open(os.path.join(directory, "s57objectclasses.csv"),
                  encoding="latin-1") as f:
            for row in csv.DictReader(f):
                try:
                    self.objl[row["Acronym"]] = int(row["Code"])
                    self.prims[row["Acronym"]] = {
                        p for p in (row["Primitives"] or "").split(";") if p}
                    self.allowed[row["Acronym"]] = {
                        a for col in ("Attribute_A", "Attribute_B",
                                      "Attribute_C")
                        for a in (row[col] or "").split(";") if a}
                except (ValueError, TypeError):
                    pass
        codes = {}
        with open(os.path.join(directory, "s57attributes.csv"),
                  encoding="latin-1") as f:
            for row in csv.DictReader(f):
                try:
                    code = int(row["Code"])
                except (ValueError, TypeError):
                    continue
                self.attl[row["Acronym"]] = code
                self.atype[row["Acronym"]] = row["Attributetype"].strip()
                codes[code] = row["Acronym"]
        path = os.path.join(directory, "s57expectedinput.csv")
        if os.path.isfile(path):
            with open(path, encoding="latin-1") as f:
                for row in csv.DictReader(f):
                    try:
                        acr = codes[int(row["Code"])]
                        self.enums.setdefault(acr, []).append(
                            (int(row["ID"]), row["Meaning"]))
                    except (KeyError, ValueError, TypeError):
                        pass

    def match_enum(self, acronym, value):
        """Fuzzy-match an OpenSeaMap value (e.g. ``yacht_harbour``) against
        the S-57 expected-input meanings.  Returns the numeric id or None."""
        words = _norm(value).split()
        if not words:
            return None
        best = None
        for ident, meaning in self.enums.get(acronym, []):
            mwords = _norm(meaning).split()
            if words == mwords:
                return ident
            if all(w in mwords for w in words):
                if best is None or len(mwords) < best[0]:
                    best = (len(mwords), ident)
        return best[1] if best else None
