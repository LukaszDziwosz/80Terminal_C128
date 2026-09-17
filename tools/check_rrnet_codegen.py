"""Guard real Oscar64 MMIO sequences which mocked hardware tests cannot cover."""
import re
import sys
from pathlib import Path

listing = Path(sys.argv[1]).read_text()
for function, expected in (
    ("write_page", [("STA", "de02"), ("STA", "de03"),
                    ("STA", "de04"), ("STA", "de05")]),
    ("receive_length", [("LDA", "de09"), ("LDA", "de08"),
                        ("LDA", "de09"), ("LDA", "de08")]),
):
    match = re.search(r"^" + function + r":.*?\n-{10,}", listing, re.M | re.S)
    if not match:
        raise SystemExit(f"RR-Net: missing {function} in generated code")
    accesses = re.findall(r"\b(LDA|LDX|LDY|STA|STX|STY) \$(de[0-9a-f]{2})\b", match[0])
    if accesses != expected:
        raise SystemExit(f"RR-Net: incorrect {function} I/O sequence: {accesses}")
print("RR-Net generated code: register selection and four FIFO header reads verified")
