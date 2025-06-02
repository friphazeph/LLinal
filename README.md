# LLinal: A C-Native Intent Execution Protocol for Language Models

**LLinal Is Not A Language.** It functions as a minimalist, C-native system designed to serve as a bridge between Large Language Models (LLMs) and host system operations. LLinal enables LLMs to execute predefined commands, transforming textual intent into precise, callable C functions.

-----

## What is LLinal?

LLinal provides a mechanism for LLMs to interact with the real world in a controlled and reliable manner. It processes `.lln` (LLinal) script files, which contain command invocations. LLinal's runtime parses these commands, validates their arguments against declared C function signatures, and dispatches them for execution. This process aims to ensure that only valid and intended operations are performed.

-----

## Key Features & Design Principles

  * **C-Native & Minimalist:** Implemented entirely in C to support efficiency and direct system control. The core components are designed for a small memory footprint and lean execution.
  * **Robust Command Parsing:** LLinal's parser is designed to extract commands (`!command(...)`) from input, ignoring non-command text. This allows for commands to be embedded within verbose or varied LLM-generated outputs.
  * **Strict Type & Signature Validation:** Command signatures are explicitly defined in C. At runtime, LLinal validates the types and number of arguments provided in an `.lln` script against these declared C function signatures.
  * **Modular Plugin System:** Custom commands are implemented as standard C functions and compiled into shared objects (`.so`). LLinal dynamically loads these plugins, providing a mechanism for extending functionality without modifying the core.
  * **Inert by default:** LLinal has *no* predefined commands. All execution is programmer-defined. This makes it safe by default, needing no sandboxing, but powerful if needed.
  * **"Live-Execute-Die" Execution Model:** Each invocation of `lln -ro` operates as a single-shot process. It loads the necessary shared objects, executes the `.lln` script, and then terminates. This simplifies resource management and prevents state persistence across separate invocations.
  * **Scripts *will* be read to the end:** If the script file was successfully opened, it will be read and interpreted to the end, unless a command function implementation crashes or exits on purpose. Execution is equivalent to calling the same functions with the same arguments in order.
  * **Custom C Preprocessor Integration:** LLinal leverages a custom C preprocessor, built into the `lln` CLI itself (`lln -p`), to automate command registration and argument signature extraction from C source files.
  * **Composability via Standard I/O:** LLinal focuses solely on command execution. It uses only `stderr` streams for communication back to the orchestrating system or LLM agent, aligning with a UNIX-like philosophy for tool integration.

-----

## Why LLinal? (Value Proposition)

  * **Reliable Execution:** LLinal provides a deterministic layer for LLM interactions. It enforces a contract between the LLM's output and system actions, ensuring that only valid and properly structured commands are executed.
  * **Direct System Control:** By operating in C, LLinal offers direct access to system resources and performance control.
  * **Clear Interface:** It provides a defined interface for LLMs to generate executable commands, contributing to more predictable LLM agent behavior.

-----

## Getting Started

To get started with LLinal, you will need a C compiler (e.g., `gcc` or `clang`) and `make`.

1.  **Clone the repository:**

    ```bash
    git clone https://github.com/friphazeph/LLinal.git
    cd LLinal
    ```

2.  **Build LLinal:**

    ```bash
    make
    ```

    This command compiles the `lln` executable and `liblln.so` library.

3.  **Install LLinal (Optional, for system-wide access):**

    ```bash
    sudo make install
    ```

    This installs the `lln` executable to `/usr/local/bin` and `liblln.so` to `/usr/local/lib`.

-----

## Defining Custom Commands

Custom commands are defined as C functions within `.c` files. They use a specific `// @cmd` annotation or the `LLN_declare_command` macro to register them with LLinal.

**Example: `src/commands/my_printer.c`**

```c
#include <lln/lln.h>
#include <stdio.h>

// @cmd !printf
void *print(char *s, int i) {
    printf("%s, %d\n", s, i);
    return NULL;
}
```

-----

## Executing LLinal Scripts (`.lln`)

Once commands are defined in your C source files, they are processed by the LLinal preprocessor and can be compiled into a `.so`. You can then create and execute `.lln` scripts.

1.  **Ensure commands are built:** Run `lln -p [input.c] [output.c]` to use the custom preprocessor, then compile them to a `.so` yourself, or use the `lln -co [input.c] [output.so]` command to both preprocess and compile to a `.so`.

2.  **Create an LLinal script file (e.g., `hello.lln`):**

    ```lln
    # This is a comment in an .lln script
    # Any text not starting with '!' is ignored by the parser

    !printf("Hello, world!", 42)
    !printf("The answer is", 42)
    ```
    In a typical workflow, this would be LLM-generated.

3.  **Run the script using `lln -ro`:**

    ```bash
    ./lln -ro hello.lln [your_so_file.so]
    ```

    **Expected Output:**

    ```
    Hello, world!, 42
    The answer is, 42
    ```

-----

## Contributing

Contributions are welcome. If you identify issues, have suggestions, or wish to contribute to the project, please open issues or submit pull requests on the GitHub repository.

-----

## License

This project is licensed under the MIT License.

See the LICENSE file for full details.

### Commercial Use & Inquiries

For commercial use scenarios, or if you are interested in potential collaboration, support, or partnership opportunities related to LLinal, please contact the author, Maxime Delhaye, at maxime.delhaye.md@gmail.com.

-----

## Author

**Maxime Delhaye** - [friphazeph](https://github.com/friphazeph)
