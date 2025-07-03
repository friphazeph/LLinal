from lln import lln_cmd, lln_run

total = 0

@lln_cmd()
def printf(s: str, i: int):
    print(f"{s}, {i}")
    global total
    total += i

lln_run("../tests/hello.lln")

print(f"Total: {total}")
