# LLinal: A C-Native Intent Execution Protocol for Language Models

**LLinal Is Not A Language.**
It is a minimalist system implemented in C that acts as a bridge between Large Language Models (LLMs) and host system operations. LLinal allows LLMs to convert textual intent into calls to predefined C functions, enabling safe and deterministic execution of commands embedded within LLM output.

---

## Quick start

Clone the repository:
```bash
git clone https://github.com/friphazeph/LLinal.git
```

Compile:
```bash
cd LLinal && make
```
Optionally, install the CLI globally:
```bash
sudo make install
```

---

## What is LLinal?

LLinal enables LLMs to interact with the real world in a controlled manner by executing commands embedded in `.lln` script files. These scripts contain invocations of commands prefixed with `!` (e.g., `!printf("Hello", 42)`). LLinal parses these commands, validates their arguments against declared C function signatures, and executes the corresponding functions.

This ensures that only well-defined and intended operations are performed, making LLM-driven automation more reliable and secure.

---

## Intended Workflow

LLinal is designed to serve as the execution backend for Large Language Models. The typical workflow is:

1. An LLM generates a `.lln` script — a plain text file containing a mix of free-form comments and executable commands prefixed with `!`.
2. This `.lln` script is passed to the LLinal runtime (`lln`), along with the shared library containing the implementation of the commands.
3. LLinal parses the script, validates commands and arguments, and executes them sequentially with full type safety.
4. The process runs to completion and then exits, ensuring a clean, isolated execution environment for each script.

This workflow enables reliable, deterministic bridging of LLM textual intent to system-level operations with strict validation and safety.

---

## Key Features

* **C-Native & Minimalist:** Fully implemented in C for efficiency and a small memory footprint.
* **Robust Command Parsing:** Extracts commands prefixed by `!` while ignoring other text, allowing free-form output around commands.
* **Strict Signature Validation:** Validates argument types and counts at runtime against declared C functions.
* **Modular Plugin System:** Commands are compiled as shared objects (`.so`) and loaded dynamically, enabling flexible extension.
* **Custom Preprocessor:** Automates command registration and argument signature extraction from C source files.
* **LLM-Friendly Syntax:** Treats any line not starting with `!` as a comment, enabling natural language or reasoning interleaved with executable commands.

---

## Design Philosophy

* **Safety by Default:** No built-in commands exist; only programmer-defined commands run. This design eliminates unintended command execution risks without requiring sandboxing.
* **Single-Run Execution Model:** Each invocation loads the necessary plugins, executes the `.lln` script fully, then terminates, preventing persistent state and simplifying resource management.
* **Simplicity & Minimalism:** LLinal focuses on a lean runtime, avoiding complex parsing logic or persistent state across runs.
* **Clear Contracts:** Enforces strict command signature validation to provide a deterministic, predictable interface between LLM output and system actions.
* **Extensibility:** Plugin architecture encourages modular growth and flexibility without changes to the core runtime.

---

## Command Line Usage

```bash
# Preprocessing:
lln -p  [input_file.c] [output_file.c]
    # Preprocess a C source file, output another C source file.

# Compilation:
lln -c  [input_file.c] [output_executable]
    # Preprocess and compile a C file with main() to a standalone executable.

lln -co [input_file.c] [output_file.so]
    # Preprocess and compile a main-less C file to a shared object (.so).

# Running:
lln -ro [input_file.lln] [input_file.so]
    # Run an .lln script using commands from a compiled shared object.

lln -rc [input_file.lln] [input_file.c]
    # Run an .lln script using commands from unprocessed main-less C source.
```

---

## Defining Custom Commands

Commands are defined as C functions in `.c` files, registered with the `// @cmd` annotation or the `LLN_declare_command` macro.

Example (`src/commands/my_printer.c`):

```c
#include <lln/lln.h>
#include <stdio.h>

// @cmd !printf
void *print(char *s, int i) {
    printf("%s, %d\n", s, i);
    return NULL;
}
```

---

## Executing LLinal Scripts

After defining commands:

1. Preprocess and compile your command source to a shared library:

```bash
lln -co src/commands/my_printer.c my_commands.so
```

2. Create a `.lln` script, e.g., `hello.lln`:

```lln
# Comments start with '#'
!printf("Hello, world!", 42)
!printf("The answer is", 42)
```

3. Run your script:

```bash
lln -ro hello.lln my_commands.so
```

Expected output:

```
Hello, world!, 42
The answer is, 42
```

---

## Contributing

Contributions, bug reports, and suggestions are welcome! Please open issues or submit pull requests on GitHub.

---

## License

MIT License — see the LICENSE file for details.

---

## Commercial Use & Inquiries

For commercial use, collaboration, or support, contact Maxime Delhaye at [maxime.delhaye.md@gmail.com](mailto:maxime.delhaye.md@gmail.com).

---

## Author

**Maxime Delhaye** — [friphazeph](https://github.com/friphazeph)
