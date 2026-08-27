#!/usr/bin/env python3
"""
generate_api_md.py
Generic tool to generate a consolidated, single-file API.md from Doxygen XML output.
Zero external dependencies (uses standard Python library only).

Features:
- Auto-discovers Interfaces, Classes, Structs, and Enums directly from Doxygen's index.xml.
- Automatically excludes internal / unneeded types (or filters by location / documented status).
- Parses parameters, return types, brief/detailed descriptions, notes, warnings, and code examples.
- Generates a dynamic Table of Contents and formatted GitHub-flavored Markdown.
"""

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import List, Dict, Any, Optional, Set

def get_node_text(element: Optional[ET.Element]) -> str:
    """Recursively extract and clean all text from an XML element, resolving ref/para/etc."""
    if element is None:
        return ""
    text_pieces = []
    
    def _extract(el):
        if el.text:
            text_pieces.append(el.text)
        for child in el:
            if child.tag == "ref":
                _extract(child)
            elif child.tag == "para":
                _extract(child)
                text_pieces.append("\n\n")
            elif child.tag == "sp":
                text_pieces.append(" ")
            elif child.tag == "linebreak":
                text_pieces.append("\n")
            else:
                _extract(child)
            if child.tail:
                text_pieces.append(child.tail)

    _extract(element)
    raw = "".join(text_pieces).strip()
    lines = [line.strip() for line in raw.split("\n")]
    return "\n".join(lines).strip()

def extract_code_from_programlisting(el: Optional[ET.Element]) -> str:
    """Extract clean code text from a Doxygen <programlisting> element."""
    if el is None:
        return ""
    code_lines = []
    for codeline in el.findall("codeline"):
        line_pieces = []
        for hl in codeline.findall("highlight"):
            def _rec(h):
                if h.text:
                    line_pieces.append(h.text)
                for child in h:
                    if child.tag == "sp":
                        line_pieces.append(" ")
                    else:
                        _rec(child)
                    if child.tail:
                        line_pieces.append(child.tail)
            _rec(hl)
        code_lines.append("".join(line_pieces))
    return "\n".join(code_lines).strip()

def parse_detaileddescription(desc_el: Optional[ET.Element]) -> Dict[str, Any]:
    """Parse detailed description element into body text, params, returns, notes, warnings, examples."""
    result = {
        "text": "",
        "params": [],
        "returns": [],
        "notes": [],
        "warnings": [],
        "see": [],
        "examples": []
    }
    if desc_el is None:
        return result

    main_text_pieces = []

    for para in desc_el.findall("para"):
        # Check for parameterlist
        for plist in para.findall("parameterlist"):
            kind = plist.get("kind")
            if kind == "param":
                for item in plist.findall("parameteritem"):
                    name_el = item.find("parameternamelist/parametername")
                    desc_el_item = item.find("parameterdescription")
                    p_name = get_node_text(name_el)
                    p_desc = get_node_text(desc_el_item)
                    if p_name:
                        result["params"].append({"name": p_name, "desc": p_desc})

        # Check for simplesect (return, note, warning, see)
        for ssect in para.findall("simplesect"):
            kind = ssect.get("kind")
            s_text = get_node_text(ssect)
            if kind == "return":
                result["returns"].append(s_text)
            elif kind == "note":
                result["notes"].append(s_text)
            elif kind == "warning":
                result["warnings"].append(s_text)
            elif kind == "see":
                result["see"].append(s_text)

        # Check for programlisting (code examples)
        for prog in para.findall("programlisting"):
            code = extract_code_from_programlisting(prog)
            if code:
                result["examples"].append(code)

        # Extract free paragraph text
        para_clone = ET.fromstring(ET.tostring(para))
        for bad in para_clone.findall("parameterlist") + para_clone.findall("simplesect") + para_clone.findall("programlisting"):
            para_clone.remove(bad)
        cleaned_para = get_node_text(para_clone)
        if cleaned_para:
            main_text_pieces.append(cleaned_para)

    result["text"] = "\n\n".join(main_text_pieces).strip()
    return result

def parse_enum(memberdef: ET.Element) -> Dict[str, Any]:
    """Parse an enum memberdef into structured data."""
    name = memberdef.findtext("name", "")
    brief = get_node_text(memberdef.find("briefdescription"))
    detailed = parse_detaileddescription(memberdef.find("detaileddescription"))
    
    values = []
    for enumvalue in memberdef.findall("enumvalue"):
        val_name = enumvalue.findtext("name", "")
        val_brief = get_node_text(enumvalue.find("briefdescription"))
        val_detail = get_node_text(enumvalue.find("detaileddescription"))
        desc = val_brief if val_brief else val_detail
        values.append({"name": val_name, "desc": desc})
        
    return {
        "name": name,
        "brief": brief,
        "detailed": detailed,
        "values": values
    }

def parse_struct(xml_path: Path) -> Optional[Dict[str, Any]]:
    """Parse a struct compounddef XML."""
    if not xml_path.exists():
        return None
    try:
        tree = ET.parse(xml_path)
    except Exception:
        return None

    compound = tree.find("compounddef")
    if compound is None:
        return None
    
    name = compound.findtext("compoundname", "")
    brief = get_node_text(compound.find("briefdescription"))
    detailed = parse_detaileddescription(compound.find("detaileddescription"))
    
    fields = []
    for section in compound.findall("sectiondef"):
        if section.get("kind") in ["public-attrib", "attrib"]:
            for member in section.findall("memberdef"):
                if member.get("kind") == "variable":
                    f_type = get_node_text(member.find("type"))
                    f_name = member.findtext("name", "")
                    f_brief = get_node_text(member.find("briefdescription"))
                    f_detail = get_node_text(member.find("detaileddescription"))
                    f_desc = f_brief if f_brief else f_detail
                    fields.append({
                        "name": f_name,
                        "type": f_type,
                        "desc": f_desc
                    })
                    
    return {
        "name": name,
        "brief": brief,
        "detailed": detailed,
        "fields": fields
    }

def parse_class_or_interface(xml_path: Path) -> Optional[Dict[str, Any]]:
    """Parse an interface or class compounddef XML."""
    if not xml_path.exists():
        return None
    try:
        tree = ET.parse(xml_path)
    except Exception:
        return None

    compound = tree.find("compounddef")
    if compound is None:
        return None
    
    name = compound.findtext("compoundname", "")
    brief = get_node_text(compound.find("briefdescription"))
    detailed = parse_detaileddescription(compound.find("detaileddescription"))
    kind = compound.get("kind", "class")
    
    methods = []
    for section in compound.findall("sectiondef"):
        if section.get("kind") in ["public-func", "func"]:
            header = section.findtext("header", "")
            for member in section.findall("memberdef"):
                if member.get("kind") == "function":
                    m_name = member.findtext("name", "")
                    if m_name.startswith("~"):
                        continue  # Skip destructors
                        
                    return_type = get_node_text(member.find("type"))
                    argsstring = member.findtext("argsstring", "")
                    m_brief = get_node_text(member.find("briefdescription"))
                    m_detailed = parse_detaileddescription(member.find("detaileddescription"))
                    
                    clean_def = f"{return_type} {m_name}{argsstring}".strip()
                    if clean_def.endswith("=0"):
                        clean_def = clean_def[:-2].strip()
                    if clean_def.startswith("virtual "):
                        clean_def = clean_def[8:].strip()
                        
                    params_info = []
                    for param in member.findall("param"):
                        p_type = get_node_text(param.find("type"))
                        p_decl = param.findtext("declname", "")
                        p_def = param.findtext("defval", "")
                        params_info.append({"name": p_decl, "type": p_type, "default": p_def})

                    methods.append({
                        "name": m_name,
                        "group": header,
                        "signature": clean_def,
                        "return_type": return_type,
                        "brief": m_brief,
                        "detailed": m_detailed,
                        "params_info": params_info
                    })

    return {
        "name": name,
        "kind": kind,
        "brief": brief,
        "detailed": detailed,
        "methods": methods
    }

def read_doxyfile_info(doxyfile_path: Path) -> Dict[str, str]:
    """Extract PROJECT_NAME, PROJECT_BRIEF, and PROJECT_NUMBER from Doxyfile if available."""
    info = {"name": "", "brief": "", "version": ""}
    if not doxyfile_path.exists():
        return info
    try:
        content = doxyfile_path.read_text(encoding="utf-8")
        for line in content.splitlines():
            line = line.strip()
            if line.startswith("PROJECT_NAME"):
                m = re.search(r'PROJECT_NAME\s*=\s*"?([^"]*)"?', line)
                if m:
                    info["name"] = m.group(1).strip()
            elif line.startswith("PROJECT_BRIEF"):
                m = re.search(r'PROJECT_BRIEF\s*=\s*"?([^"]*)"?', line)
                if m:
                    info["brief"] = m.group(1).strip()
            elif line.startswith("PROJECT_NUMBER"):
                m = re.search(r'PROJECT_NUMBER\s*=\s*"?([^"]*)"?', line)
                if m:
                    info["version"] = m.group(1).strip()
    except Exception:
        pass
    return info

def generate_markdown(xml_dir: Path, output_file: Path, title: Optional[str] = None, exclude_patterns: Optional[List[str]] = None):
    """Generate consolidated API.md dynamically from Doxygen XML index."""
    index_file = xml_dir / "index.xml"
    if not index_file.exists():
        raise FileNotFoundError(f"Doxygen index file not found: {index_file}")

    index_tree = ET.parse(index_file)
    root = index_tree.getroot()

    # Doxyfile metadata fallback
    doxyfile_info = read_doxyfile_info(xml_dir.parent.parent.parent / "Doxyfile")
    if not title:
        if doxyfile_info["name"]:
            proj_title = doxyfile_info["name"].replace("_", " ").title()
            title = f"{proj_title} — API Reference"
        else:
            title = "API Reference"

    excluded_names: Set[str] = set()
    if exclude_patterns:
        for pat in exclude_patterns:
            excluded_names.add(pat)

    # 1. Discover all compounds from index.xml
    interface_compounds = []
    struct_compounds = []
    namespace_compounds = []

    for compound in root.findall("compound"):
        c_refid = compound.get("refid", "")
        c_kind = compound.get("kind", "")
        c_name = compound.findtext("name", "")

        # Skip explicit exclusions
        if any(exc in c_name for exc in excluded_names):
            continue

        if c_kind in ["interface", "class"]:
            # Prefer interfaces or classes designated as API
            interface_compounds.append((c_refid, c_name))
        elif c_kind == "struct":
            struct_compounds.append((c_refid, c_name))
        elif c_kind == "namespace":
            namespace_compounds.append((c_refid, c_name))

    # 2. Parse Enums from all namespaces
    enums = []
    for refid, ns_name in namespace_compounds:
        ns_file = xml_dir / f"{refid}.xml"
        if ns_file.exists():
            ns_tree = ET.parse(ns_file)
            for section in ns_tree.findall("compounddef/sectiondef"):
                if section.get("kind") in ["enum", "user-defined"]:
                    for member in section.findall("memberdef"):
                        if member.get("kind") == "enum":
                            e_name = member.findtext("name", "")
                            if not any(exc in e_name for exc in excluded_names):
                                enums.append(parse_enum(member))

    # 3. Parse Structs
    structs = []
    for refid, s_name in struct_compounds:
        s_file = xml_dir / f"{refid}.xml"
        s_data = parse_struct(s_file)
        # Filter structs that have no brief description or are internal wire packets
        if s_data and (s_data["brief"] or s_data["fields"]):
            # Ignore purely internal wire frames if requested
            if any(internal in s_data["name"] for internal in ["RxPacket", "TxPacket", "PendingAck", "DecodedTxPacket", "DecodedRxPacket"]):
                continue
            structs.append(s_data)

    # 4. Parse Interfaces / Main API Classes
    interfaces = []
    for refid, i_name in interface_compounds:
        i_file = xml_dir / f"{refid}.xml"
        i_data = parse_class_or_interface(i_file)
        if i_data and i_data.get("methods"):
            interfaces.append(i_data)

    # 5. Build Markdown Content
    lines = []
    lines.append("> **Note**: This document is automatically generated from Doxygen comments in source headers.")
    lines.append("")
    lines.append(f"# {title}")
    lines.append("")
    if doxyfile_info["brief"]:
        lines.append(doxyfile_info["brief"])
        lines.append("")
    lines.append("---")
    lines.append("")

    # Table of Contents
    lines.append("## Table of Contents")
    lines.append("")
    if structs or enums:
        lines.append("- [Data Types and Structures](#data-types-and-structures)")
        for s in structs:
            s_clean = s['name'].split("::")[-1]
            lines.append(f"  - [`{s['name']}`](#{s_clean.lower()})")
        for e in enums:
            lines.append(f"  - [`{e['name']}`](#{e['name'].lower()})")

    for iface in interfaces:
        i_clean = iface['name'].split("::")[-1]
        lines.append(f"- [Public API ({iface['name']})](#public-api-{i_clean.lower()})")
        for m in iface['methods']:
            lines.append(f"  - [`{m['name']}()`](#{m['name'].lower()})")

    lines.append("")
    lines.append("---")
    lines.append("")

    # Render Data Types and Structures
    if structs or enums:
        lines.append("## Data Types and Structures")
        lines.append("")

        for s in structs:
            lines.append(f"### `{s['name']}`")
            lines.append("")
            if s['brief']:
                lines.append(s['brief'])
                lines.append("")
            if s['detailed']['text']:
                lines.append(s['detailed']['text'])
                lines.append("")

            if s['fields']:
                lines.append("| Field | Type | Description |")
                lines.append("| :--- | :--- | :--- |")
                for f in s['fields']:
                    f_desc = f['desc'].replace("\n", " ") if f['desc'] else "—"
                    lines.append(f"| `{f['name']}` | `{f['type']}` | {f_desc} |")
                lines.append("")

            if s['detailed']['examples']:
                for ex in s['detailed']['examples']:
                    lines.append("**Example:**")
                    lines.append("```cpp")
                    lines.append(ex)
                    lines.append("```")
                    lines.append("")

        for e in enums:
            lines.append(f"### `{e['name']}`")
            lines.append("")
            if e['brief']:
                lines.append(e['brief'])
                lines.append("")
            if e['detailed']['text']:
                lines.append(e['detailed']['text'])
                lines.append("")

            if e['values']:
                lines.append("| Value | Description |")
                lines.append("| :--- | :--- |")
                for v in e['values']:
                    v_desc = v['desc'].replace("\n", " ") if v['desc'] else "—"
                    lines.append(f"| `{v['name']}` | {v_desc} |")
                lines.append("")

            if e['detailed']['examples']:
                for ex in e['detailed']['examples']:
                    lines.append("**Example:**")
                    lines.append("```cpp")
                    lines.append(ex)
                    lines.append("```")
                    lines.append("")

        lines.append("---")
        lines.append("")

    # Render Interfaces / API Methods
    for iface in interfaces:
        i_clean = iface['name'].split("::")[-1]
        lines.append(f"## Public API ({iface['name']})")
        lines.append("")
        if iface['brief']:
            lines.append(iface['brief'])
            lines.append("")
        if iface['detailed']['text']:
            lines.append(iface['detailed']['text'])
            lines.append("")

        for m in iface['methods']:
            lines.append(f"### `{m['name']}()`")
            lines.append("")
            if m['brief']:
                lines.append(f"**{m['brief']}**")
                lines.append("")
            if m['detailed']['text']:
                lines.append(m['detailed']['text'])
                lines.append("")

            lines.append("```cpp")
            lines.append(f"{m['signature']};")
            lines.append("```")
            lines.append("")

            if m['detailed']['params']:
                lines.append("**Parameters:**")
                lines.append("")
                lines.append("| Parameter | Description |")
                lines.append("| :--- | :--- |")
                for p in m['detailed']['params']:
                    p_desc = p['desc'].replace("\n", " ")
                    lines.append(f"| `{p['name']}` | {p_desc} |")
                lines.append("")

            if m['detailed']['returns']:
                lines.append("**Returns:**")
                lines.append("")
                for r in m['detailed']['returns']:
                    for r_line in r.split("\n"):
                        if r_line.strip():
                            lines.append(f"* `{r_line.strip()}`")
                lines.append("")

            if m['detailed']['notes']:
                for n in m['detailed']['notes']:
                    lines.append(f"> **Note**: {n}")
                    lines.append("")
            if m['detailed']['warnings']:
                for w in m['detailed']['warnings']:
                    lines.append(f"> **Warning**: {w}")
                    lines.append("")

            if m['detailed']['examples']:
                for ex in m['detailed']['examples']:
                    lines.append("**Example:**")
                    lines.append("```cpp")
                    lines.append(ex)
                    lines.append("```")
                    lines.append("")

            lines.append("---")
            lines.append("")

    content = "\n".join(lines)
    output_file.write_text(content, encoding="utf-8")
    print(f"Generated {output_file} ({len(lines)} lines)")

def main():
    parser = argparse.ArgumentParser(description="Generate consolidated Markdown documentation from Doxygen XML.")
    parser.add_argument("--xml-dir", type=Path, default=None, help="Directory containing Doxygen XML output (default: build/docs/doxygen/xml)")
    parser.add_argument("--output", "-o", type=Path, default=None, help="Target markdown file (default: API.md in component root)")
    parser.add_argument("--title", "-t", type=str, default=None, help="Title for the documentation")
    parser.add_argument("--exclude", "-e", nargs="*", default=[], help="Type names or patterns to exclude from documentation")

    args = parser.parse_args()

    base_dir = Path(__file__).resolve().parent.parent
    xml_directory = args.xml_dir if args.xml_dir else (base_dir / "build" / "docs" / "doxygen" / "xml")
    target_md = args.output if args.output else (base_dir / "API.md")

    if not xml_directory.exists():
        print(f"Error: XML directory {xml_directory} does not exist. Run 'doxygen Doxyfile' first.", file=sys.stderr)
        sys.exit(1)

    generate_markdown(xml_directory, target_md, title=args.title, exclude_patterns=args.exclude)

if __name__ == "__main__":
    main()
