"""Search space of the parameter tuner: which Params fields move, their bounds, and the C++ header they turn into.

The space comes from a JSON spec (see infra/tuning/space.example.json):

  {
    "contexts": "group_phase",          # "group_phase" | "group" | "none": the shape of the bot's applyTuned()
    "groups": 3, "phases": 4,           # map groups (judged online by the bot) and game phases
    "namespace": "bc", "struct": "Params",
    "knobs": {
      "gamma":     {"lo": 0.70, "hi": 0.92, "per": "context"},
      "squadSize": {"lo": 1, "hi": 5, "int": true, "per": "group"},
      "wDanger":   {"lo": 0.3, "hi": 4.0}
    }
  }

"per" says how many dimensions a knob gets: "all" (default; one value for every context), "group" (one per map group,
shared by its phases) or "context" (one per (group, phase)). Dimension names are knob, knob@g and knob@g@ph.

The bot side is a header tuned_params.hpp defining

  contexts "group_phase":  inline void applyTuned(Params& p, int group, int phase) { ... }
                           inline void applyTuned(Params& p, int group) { applyTuned(p, group, 1); }
  contexts "group":        inline void applyTuned(Params& p, int group) { ... }
  contexts "none":         inline void applyTuned(Params& p, int group) { ... }   (group ignored)

which the bot includes when present. When the base bot already has a tuned_params.hpp, every field it sets is kept
(literally, as written) and only the tuned knobs are replaced, so a run over a few knobs leaves the rest of an earlier
tuning intact. Start values: the base header's values (the mean over the contexts a dimension covers), else the Params
struct's defaults in common.hpp, clamped to the bounds. A dimension that covers several contexts replaces all of them
with one value: use "per": "context" to start exactly at a per-context base.
"""
import hashlib
import json
import os
import re

FIELD = re.compile(r"^\s+(?:double|float|int|bool)\s+(\w+)\s*=\s*([-+0-9.eE]+|true|false)\s*;", re.M)
ASSIGN = re.compile(r"^\s*p\.(\w+)\s*=\s*([^;]+);", re.M)
CONTEXT = re.compile(r"if\s*\(\s*group\s*==\s*(\d+)(?:\s*&&\s*phase\s*==\s*(\d+))?\s*\)\s*\{")


class Space:
    def __init__(self, spec, common_text="", tuned_text=None):
        self.spec = spec
        self.kind = spec.get("contexts", "group_phase")
        if self.kind not in ("group_phase", "group", "none"):
            raise ValueError(f"contexts must be group_phase, group or none, not {self.kind!r}")
        self.groups = int(spec.get("groups", 3)) if self.kind != "none" else 1
        self.phases = int(spec.get("phases", 4)) if self.kind == "group_phase" else 1
        self.namespace = spec.get("namespace", "bc")
        self.struct = spec.get("struct", "Params")
        self.knobs = {}
        for k, v in spec["knobs"].items():
            per = v.get("per", "all")
            if per not in ("all", "group", "context"):
                raise ValueError(f"knob {k}: per must be all, group or context")
            if per == "context" and self.kind != "group_phase":
                per = "group"
            if self.kind == "none":
                per = "all"
            lo, hi = float(v["lo"]), float(v["hi"])
            if not hi > lo:
                raise ValueError(f"knob {k}: hi must be above lo")
            self.knobs[k] = (lo, hi, bool(v.get("int", False)), per)
        self.struct_defaults = {m.group(1): m.group(2) for m in FIELD.finditer(common_text or "")}
        if common_text:
            missing = [k for k in self.knobs if k not in self.struct_defaults]
            if missing:
                raise ValueError(f"knobs not in the bot's {self.struct} struct: {', '.join(missing)}")
        self.base = parse_header(tuned_text) if tuned_text else {}

    @classmethod
    def for_bot(cls, spec_path, bot_dir):
        with open(spec_path) as f:
            spec = json.load(f)
        common = os.path.join(bot_dir, spec.get("params_file", "common.hpp"))
        tuned = os.path.join(bot_dir, "tuned_params.hpp")
        return cls(spec, open(common).read() if os.path.exists(common) else "",
                   open(tuned).read() if os.path.exists(tuned) else None)

    # ---------------------------------------------------------------- dimensions
    def contexts(self):
        return [(g, ph) for g in range(self.groups) for ph in range(self.phases)]

    def names(self):
        out = []
        for k, (_, _, _, per) in self.knobs.items():
            if per == "all":
                out.append(k)
            elif per == "group":
                out += [f"{k}@{g}" for g in range(self.groups)]
            else:
                out += [f"{k}@{g}@{ph}" for g in range(self.groups) for ph in range(self.phases)]
        return out

    def covers(self, name):
        """The (group, phase) contexts a dimension sets."""
        parts = name.split("@")
        if len(parts) == 1:
            return self.contexts()
        if len(parts) == 2:
            return [(int(parts[1]), ph) for ph in range(self.phases)]
        return [(int(parts[1]), int(parts[2]))]

    def knob(self, name):
        return name.split("@")[0]

    def group_of(self, name):
        parts = name.split("@")
        return int(parts[1]) if len(parts) > 1 else None

    def bounds(self, name):
        lo, hi, is_int, _ = self.knobs[self.knob(name)]
        return lo, hi, is_int

    # ---------------------------------------------------------------- values
    def base_value(self, field, ctx):
        """The base bot's value of a field in a context, as a float (None if unknown)."""
        lit = self.base.get(ctx, {}).get(field)
        if lit is None:
            lit = self.struct_defaults.get(field)
        return literal(lit)

    def defaults(self):
        out = {}
        for n in self.names():
            vals = [v for v in (self.base_value(self.knob(n), c) for c in self.covers(n)) if v is not None]
            lo, hi, is_int = self.bounds(n)
            v = sum(vals) / len(vals) if vals else (lo + hi) / 2
            v = min(hi, max(lo, v))
            out[n] = int(round(v)) if is_int else round(v, 4)
        return out

    def decode(self, x):
        """[0, 1]^n -> {dimension: value} (ints rounded)"""
        out = {}
        for n, u in zip(self.names(), x):
            lo, hi, is_int = self.bounds(n)
            v = lo + min(1.0, max(0.0, float(u))) * (hi - lo)
            out[n] = int(round(v)) if is_int else round(v, 4)
        return out

    def encode(self, values):
        out = []
        for n in self.names():
            lo, hi, _ = self.bounds(n)
            out.append(min(1.0, max(0.0, (float(values[n]) - lo) / (hi - lo))))
        return out

    def carry(self, start, prev):
        """Values of an earlier run (its mean.json) into this space. A name present in both is copied; a knob whose
        shape changed gives its values across: a coarser earlier dimension (knob, knob@g) to every finer dimension it
        covers, finer earlier dimensions to a coarser one as their mean. Knobs the earlier run did not have keep their
        start values."""
        out = dict(start)
        acc = {}
        for k, v in prev.items():
            if k in out:
                out[k] = v
                continue
            base, *rest = k.split("@")
            for n in out:
                if self.knob(n) != base or n in prev:
                    continue
                parts = n.split("@")[1:]
                if parts[:len(rest)] == rest:            # earlier dimension coarser: copy down
                    out[n] = v
                elif rest[:len(parts)] == parts:         # earlier dimensions finer: average up
                    acc.setdefault(n, []).append(float(v))
        for n, vs in acc.items():
            out[n] = sum(vs) / len(vs)
        for n in out:
            lo, hi, is_int = self.bounds(n)
            x = min(hi, max(lo, float(out[n])))
            out[n] = int(round(x)) if is_int else round(x, 4)
        return out

    # ---------------------------------------------------------------- header
    def context_values(self, values):
        """{(group, phase): {field: literal}}: the base header's fields, overridden by the tuned dimensions."""
        ctx = {c: dict(self.base.get(c, {})) for c in self.contexts()}
        for n, v in values.items():
            if self.knob(n) not in self.knobs:
                continue
            _, _, is_int = self.bounds(n)
            lit = str(int(round(v))) if is_int else repr(round(float(v), 6))
            for c in self.covers(n):
                ctx[c][self.knob(n)] = lit
        return ctx

    def header(self, values, note="GENERATED by infra/tuning"):
        ctx = self.context_values(values)
        lines = [f"// {note} -- tuned {self.struct} per " + {"group_phase": "(map group, phase)", "group": "map group",
                                                                "none": "bot"}[self.kind],
                 "#pragma once", f"namespace {self.namespace} {{"]
        if self.kind == "group_phase":
            lines.append(f"inline void applyTuned({self.struct}& p, int group, int phase) {{")
            for g, ph in self.contexts():
                lines.append(f"    if (group == {g} && phase == {ph}) {{")
                lines += [f"        p.{k} = {v};" for k, v in ctx[(g, ph)].items()]
                lines.append("    }")
            lines += ["}", f"inline void applyTuned({self.struct}& p, int group) {{ applyTuned(p, group, 1); }}"]
        elif self.kind == "group":
            lines.append(f"inline void applyTuned({self.struct}& p, int group) {{")
            for g in range(self.groups):
                lines.append(f"    if (group == {g}) {{")
                lines += [f"        p.{k} = {v};" for k, v in ctx[(g, 0)].items()]
                lines.append("    }")
            lines.append("}")
        else:
            lines.append(f"inline void applyTuned({self.struct}& p, int group) {{")
            lines.append("    (void)group;")
            lines += [f"    p.{k} = {v};" for k, v in ctx[(0, 0)].items()]
            lines.append("}")
        lines += [f"}}  // namespace {self.namespace}", ""]
        return "\n".join(lines)


def literal(text):
    if text is None:
        return None
    t = str(text).strip()
    if t in ("true", "false"):
        return 1.0 if t == "true" else 0.0
    try:
        return float(t.rstrip("fF"))
    except ValueError:
        return None


def parse_header(text):
    """{(group, phase): {field: literal}} of an applyTuned header in any of the three shapes. Blocks without a
    condition (contexts "none") are context (0, 0); a group-only block is (group, 0)."""
    out = {}
    body = text
    m = re.search(r"applyTuned\([^)]*\)\s*\{", text)
    if m:
        body = text[m.end():]
    cur = (0, 0)
    for line in body.splitlines():
        c = CONTEXT.search(line)
        if c:
            cur = (int(c.group(1)), int(c.group(2) or 0))
            continue
        a = ASSIGN.match(line)
        if a:
            out.setdefault(cur, {})[a.group(1)] = a.group(2).strip()
        if line.startswith("}"):
            break
    return out


def digest(values):
    return hashlib.sha1(repr(sorted(values.items())).encode()).hexdigest()[:12]
