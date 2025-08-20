# Custom Shell Implementation

A custom Unix shell implementation written in C, featuring job control, variable management, piping, command history and built-in commands.

## Features

### Core Functionality
- **Interactive Command Line Interface** - Full shell prompt with command history
- **Command Execution** - Execute external programs and built-in commands
- **Process Management** - Background and foreground job control
- **Pipeline Support** - Chain commands with pipes (`|`)
- **Variable System** - Set, get, and manage shell variables
- **Environment Integration** - Access and modify environment variables

### Built-in Commands
- `echo [args...]` - Display text and variables
- `pwd` - Print current working directory
- `cd [directory]` - Change current working directory
- `ls` - List content of current directory
- `cat` - Display content of file
- `set VAR=value` - Set shell variables
- `unset VAR` - Remove shell variables
- `export VAR` - Set environment variables
- `env` - Display all environment variables
- `jobs` - List active jobs
- `fg [job_id]` - Bring job to foreground
- `bg [job_id]` - Send job to background
- `exit` - Exit the shell

### Advanced Features
- **Job Control** - Full support for background processes (`&`)
- **Signal Handling** - Proper handling of Ctrl+C, Ctrl+Z
- **I/O Redireection** - Full support for I/O redirection `>` `>>` `<`
- **Variable Expansion** - Support for `$VAR` syntax
- **Error Handling** - Comprehensive error reporting
- **Memory Management** - Proper allocation and cleanup

## Getting Started

### Prerequisites
- GCC compiler
- Make utility
- Linux/Unix environment
- AddressSanitizer (optional, for debugging)

### Installation

1. **Clone the repository:**
```bash
git clone <repository-url>
cd Shell
```

2. **Compile the shell:**
```bash
make
```

3. **Run the shell:**
```bash
./shell
```

### Or...

1. **Download the released executable**

2. **Add executable permission**  
If you get permission denied, run:
```bash
chmod +x shell
```

3. **Run the shell:**
```bash
./shell
```

## Usage Examples

### Basic Commands
```bash
shell:~$ pwd
/home/user
shell:~$ cd /tmp
shell:/tmp$ ls
# ... directory listing ...
```

### Variable Management
```bash
shell:~$ set NAME=John
shell:~$ echo Hello $NAME
Hello John
shell:~$ export NAME
```

### Pipeline Operations
```bash
shell:~$ ls | grep .txt | wc -l
shell:~$ cat file.txt | sort | uniq > output.txt
```

### Job Control
```bash
shell:~$ sleep 100 &
[1] 1234
shell:~$ jobs
[1]+ Running    sleep 100 &
shell:~$ fg 1
# Brings sleep command to foreground
```

## Architecture

### Core Components

**Parser (`parser.c`)**
- Splits pipelines into commands
- Tokenizes input commands
- Manages terminal access and signal handling
- Handles variable expansion in commands

**Executor (`execute.c`)**
- Manages process creation and execution
- Implements pipeline functionality
- Handles input/output redirection

**Variable System (`input.c`)**
- Manages input 
- Provides command history

**Variable System (`variables.c`)**
- Maintains shell variable linked list
- Provides get/set/unset operations
- Integrates with environment variables

**Process Manager (`proc.c`)**
- Tracks background and foreground jobs
- Manages process groups
- Provides job and process abstractization

**Built-ins (`internalfuncs.c`)**
- Implements shell built-in commands

**Built-ins (`pipelines.c`)**
- Provides command and pipeline abstractization

## Known Limitations

- No wildcard expansion (`*`, `?`)
- No command history persistence
- No tab completion
- Limited quote handling
