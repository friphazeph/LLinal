import subprocess
import inspect
from typing import Optional
from pathlib import Path
from tempfile import NamedTemporaryFile
from shutil import which
import os

if which("lln") is None:
    raise RuntimeError("'lln' executable not found in PATH, please install LLinal.")

from typing import Any, IO
type Command = dict[str, Any]
type Commands = dict[str, Command]
global_commands: Commands = {}

# ===== Plugin generation =====

LLN_BUILD_DIR: Path = Path("./.lln-build/")

def compile_plugin(c_file: Path, output_so: Path):
    compile_cmd = [
        "lln", "-co", str(c_file), str(output_so)
    ]

    subprocess.run(compile_cmd, check=True)

def hash_name(commands: Commands) -> str:
    return "lln_build.so" # TODO

def get_plugin(commands: Commands) -> Path:
    if not LLN_BUILD_DIR.exists():
        LLN_BUILD_DIR.mkdir()

    so_path = LLN_BUILD_DIR / hash_name(commands) # TODO: generate this procedurally correctly

    if not so_path.exists():
        with NamedTemporaryFile(suffix='.c', mode='w',dir=LLN_BUILD_DIR) as c_file:
            write_c_file(c_file, commands)
            compile_plugin(Path(c_file.name), so_path)

    return so_path

# ----- C gen -----

def write_c_file(f: IO, commands: Commands):
    f.write("#include <lln/lln.h>\n\n")

    for name, cmd in commands.items():
        f.write(f"// @cmd {name}\n")
        args = ""
        for i, t in enumerate(cmd['c_types']):
            args += f"{t} a{i}, "
        args = args[:-2]
        f.write(f"void *__py_gen_{cmd['fn'].__name__}({args})")
        f.write("{\n")
        f.write("\t// this is a placeholder function, its pointer is never called.\n")
        f.write("\treturn NULL;\n")
        f.write("}\n")
    f.flush()

# ===== Execution =====

# ----- ctypes def -----
import ctypes, ctypes.util

# typedef enum {
# 	ARG_INT,
# 	ARG_FLT,
# 	ARG_STR,
# 	ARG_BOOL,
# 	ARG_COUNT
# } lln_ArgType;
class ArgType(ctypes.c_int):
    pass

ARG_INT = 0
ARG_FLT = 1
ARG_STR = 2
ARG_BOOL = 3

# typedef union {
# 	int i;
# 	float f;
# 	bool b;
# 	char *s;
# } lln_ArgValue;
class ArgValue(ctypes.Union):
    _fields_ = [
        ("i", ctypes.c_int),
        ("f", ctypes.c_float),
        ("b", ctypes.c_bool),
        ("s", ctypes.c_char_p),
    ]

# typedef struct {
# 	lln_ArgType type;
# 	lln_ArgValue value;
# } lln_Arg;
class Arg(ctypes.Structure):
    _fields_ = [
        ("type", ArgType),
        ("value", ArgValue),
    ]

# typedef struct {
# 	lln_Arg *items;
# 	size_t count;
# 	size_t capacity;
# } lln_Args;
class Args(ctypes.Structure):
    _fields_ = [
        ("items", ctypes.POINTER(Arg)),
        ("count", ctypes.c_size_t),
        ("capacity", ctypes.c_size_t),
    ]

# typedef struct {
# 	const char *filename;
#
# 	size_t row;
# 	size_t col;
#
# 	const char *prev_line_start;
# 	const char *line_start;
# } Loc;
class Loc(ctypes.Structure):
    _fields_ = [
        ("filename", ctypes.c_char_p),
        ("row", ctypes.c_size_t),
        ("col", ctypes.c_size_t),
        ("prev_line_start", ctypes.c_char_p),
        ("line_start", ctypes.c_char_p),
    ]

CommandFnPtr = ctypes.CFUNCTYPE(ctypes.c_void_p, Args)

class Comm(ctypes.Structure):
	# char *name;
	# Args args;
	# bool malformed;
	#
	# Loc loc;
	# CommandFnPtr f;
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("args", Args),
        ("malformed", ctypes.c_bool),
        ("loc", Loc),
        ("f", CommandFnPtr),
    ]

# ----- run lln file -----

def unpack_args(c_args: Args):
    py_args = []
    for i in range(c_args.count):
        arg = c_args.items[i]
        t = arg.type.value
        v = arg.value
        if   t == ARG_INT:
            py_args.append(v.i)
        elif t == ARG_FLT:
            py_args.append(v.f)
        elif t == ARG_STR:
            py_args.append(v.s.decode() if v.s else None)
        elif t == ARG_BOOL:
            py_args.append(bool(v.b))
        else:
            raise ValueError(f"Unknown arg type {t}")
    return py_args

def load_commands(py_commands_path: Optional[Path]) -> Commands:
    if py_commands_path is not None:
        raise RuntimeError("Not implemented")
    else:
        return global_commands

# load lln globally
lln_path = ctypes.util.find_library('lln')
lln = ctypes.CDLL(lln_path)

def lln_run(lln_script_path: str, py_commands_path: Optional[Path] = None):
    commands = load_commands(py_commands_path)
    so_path = str(get_plugin(commands))
    lln.load_plugin(ctypes.c_char_p(
        so_path.encode('utf-8')
    ))
    lln.load_file(ctypes.c_char_p(
        lln_script_path.encode('utf-8')
    ))

    lln.next_comm.restype = ctypes.POINTER(Comm)
    comm: ctypes.POINTER(Comm) = lln.next_comm()
    while comm:
        args = unpack_args(comm.contents.args)
        py_fn = commands[comm.contents.name.decode()]['fn']
        py_fn(*args)
        comm = lln.next_comm()

# ===== Command registration =====

def lln_cmd(name=None):
    def decorator(fn):
        source_file = inspect.getfile(fn)
        sig = inspect.signature(fn)
        c_types = []
        for p in sig.parameters.values():
            if p.annotation == str:
                c_types.append("char *")
            elif p.annotation == int:
                c_types.append("int")
            elif p.annotation == float:
                c_types.append("float")
            elif p.annotation == bool:
                c_types.append("bool")
            else:
                raise TypeError(f"ERROR: LLN: Unsupported arg type {p.annotation}. Supported types: 'str', 'int', 'float', 'bool'")

        cmd_name: str = name or f"!{fn.__name__}"
        if cmd_name[0] != '!':
            raise Error(f"ERROR: LLN: command names must start with a '!'")

        global_commands[cmd_name] = {
            'fn': fn,
            'c_types': c_types,
        }
        return fn

    return decorator

