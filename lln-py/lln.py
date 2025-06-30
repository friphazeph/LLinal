import os, hashlib, importlib, inspect, subprocess, sys
from pathlib import Path
LLN_BUILD_DIR = Path("lln_build")
LLN_BUILD_DIR.mkdir(exist_ok=True)
GENERATED_SO = LLN_BUILD_DIR / "lln-py-plugin.so"
C_GEN_FILE = LLN_BUILD_DIR / "lln-py.gen.c"
CHECKSUM_FILE = LLN_BUILD_DIR / ".lln-py.checksum"
_LOG = False

from shutil import which
if which("lln") is None:
    raise RuntimeError("'lln' executable not found in PATH, please install LLinal.")

# ===== Plugin generation =====

def calculate_checksum(filepath):
    if not os.path.exists(filepath):
        return None
    with open(filepath, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()

def gen_c(commands_py_path: str, output_c: str):
    write_c_file(output_c)

    if _LOG:
        print(f"[{__name__}] Generated C wrapper: {output_c}")

def needs_rebuild(commands_py_path: str, output_so: str) -> bool:
    current_checksum = calculate_checksum(commands_py_path)
    try:
        with open(CHECKSUM_FILE, 'r') as f:
            previous_checksum = f.read().strip()
    except FileNotFoundError:
        return True
    return not os.path.exists(output_so) or current_checksum != previous_checksum

def store_checksum(commands_py_path: str):
    with open(CHECKSUM_FILE, 'w') as f:
        f.write(calculate_checksum(commands_py_path))

def compile_plugin(c_file: str, output_so: str):
    compile_cmd = [
        "lln", "-co", c_file, output_so
    ]
    if _LOG:
        print(f"[{__name__}] Compiling with: {' '.join(compile_cmd)}")

    subprocess.run(compile_cmd, check=True, capture_output=True, text=True)

    if _LOG:
        print(f"[{__name__}] Successfully compiled {output_so}")

def raise_runtime(message: str):
    raise RuntimeError(f"[{__name__}] {message}")

def compile(commands_py_path: str, output_so: str):
    if not needs_rebuild(commands_py_path, output_so):
        if _LOG:
            print(f"[{__name__}] Commands unchanged. Using existing plugin: {output_so}")
        return

    if _LOG:
        print(f"[{__name__}] Commands changed or plugin not found. Regenerating and recompiling...")

    gen_c(commands_py_path, C_GEN_FILE)

    try:
        compile_plugin(C_GEN_FILE, output_so)
        store_checksum(commands_py_path)

    except subprocess.CalledProcessError as e:
        print(f"[{__name__}] Compilation failed!\nSTDOUT:\n{e.stdout}\nSTDERR:\n{e.stderr}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"[{__name__}] An error occurred during compilation: {e}", file=sys.stderr)
        sys.exit(1)

# ===== Execution =====

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

# typedef struct {
# 	lln_ArgType *items;
# 	size_t count;
# 	size_t capacity;
# } lln_ArgTypes;
class ArgTypes(ctypes.Structure):
    _fields_ = [
        ("items", ctypes.POINTER(ArgType)),
        ("count", ctypes.c_size_t),
        ("capacity", ctypes.c_size_t),
    ]

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

# typedef struct {
# 	const char *name;
# 	lln_ArgTypes signature;
#
# 	lln_CommandFnPtr fnptr;
# } lln_Callable;
class Callable(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("signature", ArgTypes),
        ("fnptr", CommandFnPtr),
    ]

# typedef struct {
# 	lln_Callable *items;
# 	size_t count;
# 	size_t capacity;
# 	void (* pre)(void);
# 	void (* post)(void);
# } lln_Callables;
class Callables(ctypes.Structure):
    _fields_ = [
        ("items", ctypes.POINTER(Callable)),
        ("count", ctypes.c_size_t),
        ("capacity", ctypes.c_size_t),
        ("pre", ctypes.CFUNCTYPE(None)),
        ("post", ctypes.CFUNCTYPE(None)),
    ]

# typedef struct {
# 	char *content;
# 	size_t len;
# 	size_t cap;
# } lln_StringBuilder;
class StringBuilder(ctypes.Structure):
    _fields_ = [
        ("content", ctypes.c_char_p),
        ("len", ctypes.c_size_t),
        ("cap", ctypes.c_size_t),
    ]

# typedef struct {
# 	Loc loc;
# 	char *start;
# 	size_t len;
# 	TokKind kind;
# 	char *text_view;
# } Token;
class Token(ctypes.Structure):
    _fields_ = [
        ("loc", Loc),
        ("start", ctypes.c_char_p),
        ("len", ctypes.c_size_t),
        ("kind", ctypes.c_int),
        ("text_view", ctypes.c_char_p),
    ]

# typedef struct {
# 	const char *content;
# 	char *cur;
# 	Loc loc;
# 	Token tok;
# 	StringBuilder sb_tok_text;
# 	Comm comm;
# } Lexer;
class Lexer(ctypes.Structure):
    _fields_ = [
        ("content", ctypes.c_char_p),
        ("cur", ctypes.c_char_p),
        ("loc", Loc),
        ("tok", Token),
        ("sb_tok_text", StringBuilder),
        ("comm", Comm),
    ]

def unpack_args(args_struct: Args):
    py_args = []
    for i in range(args_struct.count):
        arg = args_struct.items[i]
        t = arg.type.value
        v = arg.value
        if t == 0:  # ARG_INT
            py_args.append(v.i)
        elif t == 1:  # ARG_FLT
            py_args.append(v.f)
        elif t == 2:  # ARG_STR
            py_args.append(v.s.decode() if v.s else None)
        elif t == 3:  # ARG_BOOL
            py_args.append(bool(v.b))
        else:
            raise ValueError(f"Unknown arg type {t}")
    return py_args

def execute(lln_script_path: str, commands_py_path: str):
    so_path = os.path.abspath(GENERATED_SO)
    try:
        plugin = ctypes.CDLL(so_path)
        plugin.__lln_preproc_register_commands.argtypes = []
        plugin.__lln_preproc_register_commands.restype = None
        plugin.__lln_preproc_register_commands()
        callables = Callables.in_dll(plugin, "__lln_preproc_callables")
        # print(callables.count)

        c_to_py_fn = {}
        for i in range(callables.count):
            call = callables.items[i]
            cfn = ctypes.cast(call.fnptr, ctypes.c_void_p).value
            c_to_py_fn[cfn] = commands[call.name.decode()]["fn"]
        
        liblln = ctypes.util.find_library("lln")
        if liblln == None:
            print("Could not find liblln.")
            exit(1)
        clln = ctypes.CDLL(liblln)
        # StringBuilder file = {0};
        # read_whole_file(&file, filename);
        file = StringBuilder()
        clln.lln_read_whole_file.argtypes = [ctypes.POINTER(StringBuilder), ctypes.c_char_p]
        clln.lln_read_whole_file.restype = None
        clln.lln_read_whole_file(ctypes.byref(file), lln_script_path.encode("utf-8"))
        # Lexer l = {0};
        # lexer_init(&l, file.content, filename);
        l = Lexer()
        clln.lexer_init.argtypes = [ctypes.POINTER(Lexer), ctypes.c_char_p, ctypes.c_char_p]
        clln.lexer_init.restype = None
        clln.lexer_init(ctypes.byref(l), file.content, lln_script_path.encode("utf-8"))

        # Comm *lexer_next_valid_comm(Lexer *l, const Callables *c)
        clln.lexer_next_valid_comm.argtypes = [ctypes.POINTER(Lexer), ctypes.POINTER(Callables)]
        clln.lexer_next_valid_comm.restype = ctypes.POINTER(Comm)
        next_comm = clln.lexer_next_valid_comm
        c = next_comm(ctypes.byref(l), ctypes.byref(callables))
        while c:
            # print(f"Lexer token kind: {l.tok.kind}")
            fn_ptr_val = ctypes.cast(c.contents.f, ctypes.c_void_p).value
            pyfn = c_to_py_fn.get(fn_ptr_val)
            if pyfn is None:
                print(f"Unknown command function ptr {fn_ptr_val}, skipping")
            else:
                args = unpack_args(c.contents.args)
            pyfn(*args)
            c = next_comm(ctypes.byref(l), ctypes.byref(callables))

    except subprocess.CalledProcessError as e:
        print(f"[{__name__}] LLinal execution failed!\nSTDOUT:\n{e.stdout}\nSTDERR:\n{e.stderr}", file=sys.stderr)
        sys.exit(1)

def lln_run(lln_script_path: str, commands_py_path: str):
    spec = importlib.util.spec_from_file_location("user_commands", commands_py_path)
    if not spec:
        raise ImportError(f"Cannot find {commands_py_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    if _LOG:
        print(f"[{__name__}] Loaded commands from {commands_py_path}")

    compile(commands_py_path, GENERATED_SO)
    execute(lln_script_path, commands_py_path)

# ===== Command registration =====

commands = {}
def lln_cmd(name=None):
    def decorator(fn):
        source_file = inspect.getfile(fn)
        sig = inspect.signature(fn)
        c_types = []
        for p in sig.parameters.values():
            if p.annotation == str:
                c_types.append("char* ")
            elif p.annotation == int:
                c_types.append("int")
            elif p.annotation == float:
                c_types.append("float")
            elif p.annotation == bool:
                c_types.append("bool")
            else:
                raise TypeError(f"ERROR: LLN: Unsupported arg type {p.annotation}. Supported types: 'str', 'int', 'float', 'bool'")

        cmd_name = name or f"!{fn.__name__}"
        if cmd_name[0] != '!':
            raise Error(f"ERROR: LLN: command names must start with a '!'")
        commands[cmd_name] = {
            "fn": fn,
            "c_types": c_types,
        }
        return fn
    return decorator

def gen_decl(f, cmd):
    args = ""
    for i, t in enumerate(cmd["c_types"]):
        args += f"{t} a{i}, "
    args = args[:-2]

    f.write(f"void *__py_gen_{cmd["fn"].__name__}({args})")

def write_c_file(output_c: str):
    with open(output_c, 'w') as f:
        f.write("#include <lln/lln.h>\n\n")

        for name, cmd in commands.items():
            f.write(f"// @cmd {name}\n")
            gen_decl(f, cmd)
            f.write("{\n")
            f.write("\t// this is a placeholder function, its pointer is never called\n")
            f.write("\t// it is only used for command registration\n")
            f.write("\treturn NULL;\n")
            f.write("}\n")
