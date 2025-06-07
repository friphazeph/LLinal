import inspect, os, hashlib, subprocess, sys, sysconfig, importlib.util
from pathlib import Path
LLN_BUILD_DIR = Path("lln_build")
LLN_BUILD_DIR.mkdir(exist_ok=True)
GENERATED_SO = LLN_BUILD_DIR / "lln-py-plugin.so"
C_GEN_FILE = LLN_BUILD_DIR / "lln-py.gen.c"
CHECKSUM_FILE = LLN_BUILD_DIR / ".lln-py.checksum"
_LOG = False

from shutil import which
if which("lln") is None:
    raise RuntimeError("'lln' executable not found in PATH")

def calculate_checksum(filepath):
    if not os.path.exists(filepath):
        return None
    with open(filepath, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()

def gen_c(commands_py_path: str, output_c: str):
    spec = importlib.util.spec_from_file_location("user_commands", commands_py_path)
    if not spec:
        raise ImportError(f"Cannot find {commands_py_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    if _LOG:
        print(f"[{__name__}] Loaded commands from {commands_py_path}")

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

def get_python_lib_flag() -> str:
    libname = sysconfig.get_config_var("LDLIBRARY")
    if not libname:
        raise_runtime("Could not determine Python library name from sysconfig.")

    if libname.startswith("lib") and libname.endswith(".so"):
        return f"-l{libname[3:-3]}"
    elif libname.startswith("lib") and ".a" in libname:
        return f"-l{libname[3:].split('.a')[0]}"
    else:
        raise_runtime(f"Unexpected library name format: {libname}")

def compile_plugin(c_file: str, output_so: str):
    import sysconfig
    python_include = sysconfig.get_path('include') or raise_runtime("Could not find Python include path")

    cflags = parse_flags("CFLAGS", "CPPFLAGS", "LDCXXFLAGS")
    ldflags = parse_flags("LDFLAGS", "LIBS")

    compile_cmd = [
        "cc", "-fPIC", "-shared", "-o", output_so, c_file,
        f"-I{python_include}",
        *cflags,
        get_python_lib_flag(),
        *ldflags,
        "-Wl,--no-as-needed",
        "-Wl,--as-needed"
    ]
    if _LOG:
        print(f"[{__name__}] Compiling with: {' '.join(compile_cmd)}")

    subprocess.run(compile_cmd, check=True, capture_output=True, text=True)

    if _LOG:
        print(f"[{__name__}] Successfully compiled {output_so}")

def parse_flags(*flag_keys: str) -> list[str]:
    import sysconfig
    combined = " ".join(sysconfig.get_config_var(key) or "" for key in flag_keys)
    return [flag for flag in combined.split() if flag.strip()]

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

def execute(lln_script_path: str, commands_py_path: str):
    absolute_plugin_path = os.path.abspath(GENERATED_SO)
    try:
        lln_cmd = ["lln", "-ro", lln_script_path, absolute_plugin_path]
        env = os.environ.copy()
        commands_dir = os.path.abspath(os.path.dirname(commands_py_path))
        if "PYTHONPATH" in env:
            env["PYTHONPATH"] = f"{env['PYTHONPATH']}{os.pathsep}{commands_dir}"
        else:
            env["PYTHONPATH"] = commands_dir
        if _LOG:
            print(f"[{__name__}] Executing LLinal: {' '.join(lln_cmd)} (with PYTHONPATH={env['PYTHONPATH']})")
        subprocess_cwd = os.path.abspath(os.path.dirname(lln_script_path))
        subprocess.run(lln_cmd, check=True, env=env, cwd=subprocess_cwd)
        
        if _LOG:
            print(f"[{__name__}] LLinal execution completed.")
    except subprocess.CalledProcessError as e:
        print(f"[{__name__}] LLinal execution failed!\nSTDOUT:\n{e.stdout}\nSTDERR:\n{e.stderr}", file=sys.stderr)
        sys.exit(1)
    except FileNotFoundError:
        print(f"[{__name__}] Error: 'lln' executable not found. Make sure it's in your PATH.", file=sys.stderr)
        sys.exit(1)

def lln_run(lln_script_path: str, commands_py_path: str):
    compile(commands_py_path, GENERATED_SO)
    execute(lln_script_path, commands_py_path)


commands = []

def lln_cmd(name=None):
    def decorator(fn):
        source_file = inspect.getfile(fn)
        module_path = os.path.relpath(source_file)
        module_name = module_path.replace('/', '.').replace('\\', '.').removesuffix('.py')
        sig = inspect.signature(fn)
        arg_types = []
        for p in sig.parameters.values():
            if p.annotation == str:
                arg_types.append("ARG_STR")
            elif p.annotation == int:
                arg_types.append("ARG_INT")
            elif p.annotation == float:
                arg_types.append("ARG_FLT")
            elif p.annotation == bool:
                arg_types.append("ARG_BOOL")
            else:
                raise TypeError(f"ERROR: LLN: Unsupported arg type {p.annotation}. Supported types: 'str', 'int', 'float', 'bool'")

        cmd_name = name or f"!{fn.__name__}"
        if cmd_name[0] != '!':
            raise Error(f"ERROR: LLN: command names must start with a '!'")
        commands.append({
            "name": cmd_name,
            "fnname": fn.__name__,
            "file": module_name,
            "arg_types": arg_types,
        })
        return fn
    return decorator

def gen_args(f, cmd):
    for i, a in enumerate(cmd["arg_types"]):
        carg = f"carg{i}"
        parg = f"parg{i}"
        if a == "ARG_INT":
            f.write(f"\tint {carg} = LLN_arg_int({i});\n")
            f.write(f"\tPyObject *{parg} = PyLong_FromLong({carg});\n")
        elif a == "ARG_BOOL":
            f.write(f"\tbool {carg} = LLN_arg_bool({i});\n")
            f.write(f"\tPyObject *{parg} = PyBool_FromLong({carg});\n")
        elif a == "ARG_STR":
            f.write(f"\tchar *{carg} = LLN_arg_str({i});\n")
            f.write(f"\tPyObject *{parg} = PyUnicode_FromString({carg});\n")
        elif a == "ARG_FLT":
            f.write(f"\tfloat {carg} = LLN_arg_flt({i});\n")
            f.write(f"\tPyObject *{parg} = PyFloat_FromDouble((double){carg});\n")


def gen_py_call(f, cmd):
    args_list = ", ".join(f"parg{i}" for i in range(len(cmd["arg_types"])))
    f.write(f"\tPyObject *pArgs = PyTuple_Pack({len(cmd['arg_types'])}, {args_list});\n")
    f.write(f"\tPyObject *pResult = safe_py_call(pFunc, pArgs);\n")
    f.write(f"\tPy_DECREF(pResult);\n")


def gen_py_cleanup(f, cmd):
    f.write(f'\tPy_DECREF(pArgs);\n')
    f.write(f'\tPy_DECREF(pFunc);\n')
    for i in range(len(cmd["arg_types"])):
        f.write(f'\tPy_DECREF(parg{i});\n')

def gen_decl(f, cmd):
    f.write(f'LLN_declare_command_custom_name("{cmd["name"]}", __py_{cmd["fnname"]}')
    for a in cmd["arg_types"]:
        f.write(f", {a}")
    f.write(") ")

def write_c_file(output_c: str):
    with open(output_c, 'w') as f:
        f.write("#define __LLN_PREPROCESSED_FILE\n")
        f.write("#include <lln/lln.h>\n")
        f.write("#include <Python.h>\n\n")

        f.write("""\
static PyObject* import_py_function(const char* modname, const char* fnname) {
    PyObject *mod = PyImport_ImportModule(modname);
    if (!mod) {
        PyErr_Print();
        exit(1);
    }

    PyObject *fn = PyObject_GetAttrString(mod, fnname);
    if (!fn || !PyCallable_Check(fn)) {
        PyErr_Print();
        Py_XDECREF(fn);
        Py_DECREF(mod);
        exit(1);
    }

    Py_DECREF(mod); // Drop module reference, keep function alive
    return fn;
}

static PyObject* safe_py_call(PyObject *func, PyObject *args) {
    PyObject *res = PyObject_CallObject(func, args);
    if (!res) {
        PyErr_Print();
        exit(1);
    }
    return res;
}

\n""")

        for cmd in commands:
            gen_decl(f, cmd)
            f.write("{\n")
            f.write(f'\tPyObject *pFunc = import_py_function("{cmd["file"]}", "{cmd["fnname"]}");\n')
            gen_args(f, cmd)
            gen_py_call(f, cmd)
            gen_py_cleanup(f, cmd)
            f.write("\treturn NULL;\n")
            f.write("}\n")

        f.write("\nvoid __lln_preproc_register_commands(void) {\n")
        f.write("\tPy_Initialize();\n")
        for cmd in commands:
            f.write(f'\tLLN_register_command(&__lln_preproc_callables, __py_{cmd["fnname"]});\n')
        f.write("}\n")
