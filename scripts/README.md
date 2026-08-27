# Documentation Generator Scripts

This folder contains generic Python utilities for generating consolidated Markdown API references from Doxygen XML output.

---

## `generate_api_md.py`

A standalone, zero-dependency Python script that converts Doxygen XML documentation into a clean, GitHub-flavored `API.md` file.

### Prerequisites

1. **Doxygen**: Ensure `doxygen` is installed (`sudo apt install doxygen` on Debian/Ubuntu).
2. **XML Output Enabled**: Make sure your `Doxyfile` has `GENERATE_XML = YES` and points to the relevant public headers.

---

### Basic Usage

1. Create the output directory and generate the Doxygen XML:
   ```bash
   mkdir -p build/docs/doxygen
   doxygen Doxyfile
   ```

2. Run the generator script:
   ```bash
   python3 scripts/generate_api_md.py
   ```

By default, the script:
- Reads XML files from `build/docs/doxygen/xml` (relative to the component root).
- Automatically discovers all public interfaces, classes, structs, and enums from `index.xml`.
- Extracts project name and brief description from `Doxyfile` (if present).
- Writes the consolidated markdown file to `API.md` at the component root.

---

### Command-Line Arguments & Options

You can customize the generation by passing command-line options:

```bash
python3 scripts/generate_api_md.py [OPTIONS]
```

| Argument | Short | Description | Default |
| :--- | :--- | :--- | :--- |
| `--xml-dir` | | Path to the directory containing Doxygen XML output. | `build/docs/doxygen/xml` |
| `--output` | `-o` | Path to the output Markdown file. | `API.md` |
| `--title` | `-t` | Custom title for the Markdown document header. | Extracted from `Doxyfile` or `API Reference` |
| `--exclude` | `-e` | One or more type names or pattern substrings to exclude from the output. | None |
| `--help` | `-h` | Display the help message and options. | |

---

### Examples

#### 1. Custom Title and Output Path
```bash
python3 scripts/generate_api_md.py \
    --output docs/PUBLIC_API.md \
    --title "Smart Farm ESP-NOW Component Reference"
```

#### 2. Specifying Custom XML Directory
```bash
python3 scripts/generate_api_md.py \
    --xml-dir /path/to/custom/doxygen/xml \
    --output /path/to/output/API.md
```

#### 3. Excluding Specific Internal Types
```bash
python3 scripts/generate_api_md.py \
    --exclude InternalHelper PrivateConfig
```
