#!/usr/bin/env python3
"""Turn gettext msgctxt values into Qt TS context names after lconvert."""

import argparse
import os
import tempfile
import xml.etree.ElementTree as ET


def restore_contexts(input_path: str, output_path: str) -> None:
    tree = ET.parse(input_path)
    root = tree.getroot()
    contexts = root.findall("context")
    if len(contexts) != 1 or contexts[0].findtext("name"):
        raise ValueError("expected one unnamed lconvert context")

    original = contexts[0]
    grouped: dict[str, list[ET.Element]] = {}
    for message in original.findall("message"):
        comment = message.find("comment")
        if comment is None or not comment.text:
            raise ValueError("translation is missing a gettext msgctxt")
        context_name = comment.text
        message.remove(comment)
        grouped.setdefault(context_name, []).append(message)

    root.remove(original)
    for context_name, messages in grouped.items():
        context = ET.SubElement(root, "context")
        ET.SubElement(context, "name").text = context_name
        context.extend(messages)

    ET.indent(tree, space="    ")
    parent = os.path.dirname(os.path.abspath(output_path))
    with tempfile.NamedTemporaryFile(mode="wb", dir=parent, delete=False) as temporary:
        temporary.write(b'<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n')
        temporary.write(ET.tostring(root, encoding="utf-8"))
        temporary.write(b"\n")
        temporary_path = temporary.name
    os.replace(temporary_path, output_path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("output")
    arguments = parser.parse_args()
    restore_contexts(arguments.input, arguments.output)
