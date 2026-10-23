"""Crash-safe file writes for long tuning runs.

`json.dump(obj, open(tmp, "w"))` followed by os.replace can install a truncated file when the disk is full: the write
error only surfaces when the unreferenced file object is garbage collected, after the rename. Here every write is
closed, flushed and fsynced before the rename, and a failure raises instead of replacing the previous good file.
"""
import json
import os
import pickle


def write_bytes(path, data):
    tmp = f"{path}.tmp{os.getpid()}"
    try:
        with open(tmp, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)


def write_text(path, text):
    write_bytes(path, text.encode())


def write_json(path, obj, **kw):
    write_text(path, json.dumps(obj, **kw))


def write_pickle(path, obj):
    write_bytes(path, pickle.dumps(obj))


def append_line(path, line):
    with open(path, "a") as f:
        f.write(line + "\n")
        f.flush()
        os.fsync(f.fileno())
