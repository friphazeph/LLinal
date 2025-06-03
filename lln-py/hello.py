from lln import lln_cmd

@lln_cmd()
def printf(s: str, i: int):
    print(f"{s}, {i}")
