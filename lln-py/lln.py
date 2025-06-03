import inspect
import atexit
import os
import hashlib
import subprocess
import sys
import importlib.util

_LLN_BUILD_DIR = "./lln_build/"
if not os.path.isdir(_LLN_BUILD_DIR):
    os.makedirs(_LLN_BUILD_DIR)
_GENERATED_SO_NAME = _LLN_BUILD_DIR + "lln_py_plugin.so"
_C_GEN_FILE = _LLN_BUILD_DIR + "lln-py.gen.c"
_CHECKSUM_FILE = _LLN_BUILD_DIR + ".lln_py_checksum"

def _calculate_checksum(filepath):
    if not os.path.exists(filepath):
        return None
    with open(filepath, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()

def lln_run(lln_script_path: str, commands_py_path: str):
    commands_py_checksum = _calculate_checksum(commands_py_path)
    last_checksum = None
    if os.path.exists(_CHECKSUM_FILE):
        with open(_CHECKSUM_FILE, 'r') as f:
            last_checksum = f.read().strip()

    needs_recompile = False
    if not os.path.exists(_GENERATED_SO_NAME) or commands_py_checksum != last_checksum:
        needs_recompile = True
        print(f"[{__name__}] Commands changed or plugin not found. Regenerating and recompiling...")

        # 1. Dynamically load the commands.py to populate lln.commands
        spec = importlib.util.spec_from_file_location("lln_user_commands", commands_py_path)
        if spec is None:
            raise ImportError(f"Could not find commands file: {commands_py_path}")
        user_commands_module = importlib.util.module_from_spec(spec)
        sys.modules["lln_user_commands"] = user_commands_module
        spec.loader.exec_module(user_commands_module)
        print(f"[{__name__}] Loaded commands from {commands_py_path}")

        # 2. Generate C code
        write_c_file()
        print(f"[{__name__}] Generated C wrapper: {_C_GEN_FILE}")

        # 3. Compile the C code
        try:
            import sysconfig
            python_include_dir = sysconfig.get_path('include')
            if python_include_dir is None:
                raise ValueError("Could not determine Python include directory using sysconfig.get_path('include').")

            raw_cflags_str = (sysconfig.get_config_var("CFLAGS") or "") + " " + \
                             (sysconfig.get_config_var("CPPFLAGS") or "") + " " + \
                             (sysconfig.get_config_var("LDCXXFLAGS") or "")
            parsed_cflags = [flag for flag in raw_cflags_str.split() if flag.strip()]

            raw_ldflags_str = (sysconfig.get_config_var("LDFLAGS") or "") + " " + \
                              (sysconfig.get_config_var("LIBS") or "")
            parsed_ldflags = [flag for flag in raw_ldflags_str.split() if flag.strip()]


            compile_cmd = [
                "cc", "-fPIC", "-shared", "-o", _GENERATED_SO_NAME, _C_GEN_FILE,
                f"-I{python_include_dir}", # *** THIS IS THE CRUCIAL FIX ***
                *parsed_cflags, # Add other CFLAGS obtained from sysconfig
                # "-L/usr/lib", # This might already be covered by parsed_ldflags, remove if causing issues
                "-lpython3.13", # Explicitly link against libpython
                *parsed_ldflags, # Add LDFLAGS obtained from sysconfig
                "-Wl,--no-as-needed", # Essential for linking Python symbols
                "-Wl,--as-needed"
            ]
            print(f"[{__name__}] Compiling with: {' '.join(compile_cmd)}")
            subprocess.run(compile_cmd, check=True, capture_output=True, text=True)
            print(f"[{__name__}] Successfully compiled {_GENERATED_SO_NAME}")

            # Store new checksum
            with open(_CHECKSUM_FILE, 'w') as f:
                f.write(commands_py_checksum)

        except subprocess.CalledProcessError as e:
            print(f"[{__name__}] Compilation failed!\nSTDOUT:\n{e.stdout}\nSTDERR:\n{e.stderr}", file=sys.stderr)
            sys.exit(1)
        except Exception as e:
            print(f"[{__name__}] An error occurred during compilation: {e}", file=sys.stderr)
            sys.exit(1)

    else:
        print(f"[{__name__}] Commands unchanged. Using existing plugin: {_GENERATED_SO_NAME}")


    # 4. Execute the LLinal C binary
    absolute_plugin_path = os.path.abspath(_GENERATED_SO_NAME)
    try:
        lln_executable_name = "lln" # Or absolute path if not in PATH
        lln_cmd = [lln_executable_name, "-ro", lln_script_path, absolute_plugin_path]
        env = os.environ.copy()
        commands_dir = os.path.abspath(os.path.dirname(commands_py_path))
        if "PYTHONPATH" in env:
            env["PYTHONPATH"] = f"{env['PYTHONPATH']}{os.pathsep}{commands_dir}"
        else:
            env["PYTHONPATH"] = commands_dir
        print(f"[{__name__}] Executing LLinal: {' '.join(lln_cmd)} (with PYTHONPATH={env['PYTHONPATH']})")
        subprocess_cwd = os.path.abspath(os.path.dirname(lln_script_path))
        subprocess.run(lln_cmd, check=True, env=env, cwd=subprocess_cwd)
        
        print(f"[{__name__}] LLinal execution completed.")
    except subprocess.CalledProcessError as e:
        print(f"[{__name__}] LLinal execution failed!\nSTDOUT:\n{e.stdout}\nSTDERR:\n{e.stderr}", file=sys.stderr)
        sys.exit(1)
    except FileNotFoundError:
        print(f"[{__name__}] Error: 'lln' executable not found. Make sure it's in your PATH.", file=sys.stderr)
        sys.exit(1)

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
        if (a == "ARG_INT"):
            f.write(f"\tint carg{i} = LLN_arg_int({i});\n")
            f.write(f"\tPyObject *parg{i} = PyLong_FromLong(carg{i});\n")
        elif (a == "ARG_BOOL"):
            f.write(f"\tbool carg{i} = LLN_arg_bool({i});\n")
            f.write(f"\tPyObject *parg{i} = PyBool_FromLong(carg{i});\n")
        elif (a == "ARG_STR"):
            f.write(f"\tchar *carg{i} = LLN_arg_str({i});\n")
            f.write(f"\tPyObject *parg{i} = PyUnicode_FromString(carg{i});\n")
        elif (a == "ARG_FLT"):
            f.write(f"\tfloat carg{i} = LLN_arg_flt({i});\n")
            f.write(f"\tPyObject *parg{i} = PyFloat_FromDouble((double)carg{i});\n")

def gen_py_setup(f, cmd):
    f.write(
        f"""
    PyObject *mod = PyImport_ImportModule(\"{cmd["file"]}\");
    if (!mod) {{
        PyErr_Print();
        exit(1);
    }}""")
    f.write(
        f"""
    PyObject *pFunc = PyObject_GetAttrString(mod, "{cmd["fnname"]}");
    if (!pFunc || !PyCallable_Check(pFunc)) {{
        PyErr_Print();
        Py_XDECREF(pFunc);
        Py_DECREF(mod);
        exit(1);
    }}\n""")

def gen_py_call(f, cmd):
    args_list = ", ".join(f"parg{i}" for i in range(len(cmd["arg_types"])))
    f.write(f"\tPyObject *pArgs = PyTuple_Pack({len(cmd['arg_types'])}, {args_list});\n")
    f.write(f'\tPyObject *pResult = PyObject_CallObject(pFunc, pArgs);')
    f.write(f'\tif (pResult) Py_DECREF(pResult);\n')
    f.write(f'\telse {{PyErr_Print(); exit(1);}}\n')

def gen_py_cleanup(f, cmd):
    f.write(f'\tPy_DECREF(pArgs);\n')
    f.write(f'\tPy_DECREF(pFunc);\n')
    f.write(f'\tPy_DECREF(mod);\n')
    for i in range(len(cmd["arg_types"])):
        f.write(f'\tPy_DECREF(parg{i});\n')

def gen_decl(f, cmd):
    f.write(f"LLN_declare_command_custom_name(\"{cmd["name"]}\", __py_{cmd["fnname"]}")
    for a in cmd["arg_types"]:
        f.write(f", {a}")
    f.write(f")")

def write_c_file():
    with open(_C_GEN_FILE, 'w') as f:
        f.write("#define __LLN_PREPROCESSED_FILE\n")
        f.write("#include <lln/lln.h>\n")
        f.write("#include <Python.h>\n\n\n")

        for cmd in commands:
            gen_decl(f, cmd)
            f.write("{\n")
            gen_py_setup(f, cmd)
            gen_args(f, cmd)
            gen_py_call(f, cmd)
            gen_py_cleanup(f, cmd)
            f.write(f"\treturn NULL;\n")
            f.write("}\n")
        f.write("\n\n")
        f.write("void __lln_preproc_register_commands(void) {\n")
        f.write("\tPy_Initialize();\n")
        for cmd in commands:
            f.write(f"\tLLN_register_command(&__lln_preproc_callables, __py_{cmd["fnname"]});\n")
        f.write("}\n")


atexit.register(write_c_file)
