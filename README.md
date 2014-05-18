# pin-instat: X86 Instruction Profiler

**pin-instat** is a [PIN tool](https://software.intel.com/en-us/articles/pintool/) to collect runtime information of each instructions. The information collected includes:

* **Opcode:** This is the same as any tradition disassembler.
* **Number of time the instruction is executed:** pin-instat only shows instruction executed at least once, so its output can be is much more clean than a static disassembler.
* **Value of source register:** pin-instat can't keep all value history. Only min and max value of the source register is kept.
* **The number of times the branch taken** (only for condition branch instructions)
* **Function entry points:** In pin-instat, an instruction is a function entry point iff there is a *call* to it. pin-instat can sometimes get better information comparing to a static disassembler. This is especially true for programs with lots of indirect calls, such as C++ virtual method.
* **Branch target**: For indirect branch or call, there can be mutiple targets. It only shows the lowest and highest ones.

## Example
Let's go through an example to demostrate how to use pin-instat.

First, let's write a simple porgram. Note that pin-instat doesn't require any program source code.

```
$ cat hello.c
#include <stdlib.h>
#include <stdio.h>

static int fibo (int n)
{
        if (n <= 2)
                return 1;
        else
                return fibo(n - 1) + fibo(n -2);
}

int main (int argc, char *argv[])
{
        printf("%d\n", fibo(atoi(argv[1])));
        return 0;
}

$ gcc -Wall -O -o hello hello.c
$ ./hello 10
55
```

To profile the program, let's run it through PIN.
```
$ PINDIR/pin.sh -t TOOLDIR/instat.so -- ./hello 10
55
```

The program executed by PIN should behave the same as we run it directly, except that the former is a lot (5 to 20 times) slower. The profiling information is saved in `instat.tsv`. The file is a tab-separated-value file.

Here is the part for `fibo()`

addr | opcode | exec count | source operand | min | max | extra info
---- | ------ | ---------- | -------------- | --- | --- | ----------
400580 | push rbp | 109 | rbp | 0 | 22 | entry: [hello].fibo
400581 | push rbx | 109 | rbx | 0 | a
400582 | sub rsp, 0x8 | 109 | rsp | 7fffb2b67d38 | 7fffb2b67e38
400586 | mov ebx, edi | 109 | edi | 1 | a
400588 | mov eax, 0x1 | 109 | - | - | -
40058d | cmp edi, 0x2 | 109 | edi | 1 | a
400590 | jle 0x4005a6 | 109 | -> | 4005a6 | 4005a6 | brtaken: 55
400592 | lea edi, ptr [rdi-0x1] | 54 | rdi | 3 | a
400595 | call 0x400580 | 54 | -> | 400580 | 400580 | target: [hello].fibo
40059a | mov ebp, eax | 54 | eax | 1 | 22
40059c | lea edi, ptr [rbx-0x2] | 54 | rbx | 3 | a
40059f | call 0x400580 | 54 | -> | 400580 | 400580 | target: [hello].fibo
4005a4 | add eax, ebp | 54 | eax | 1 | 15
4005a6 | add rsp, 0x8 | 109 | rsp | 7fffb2b67d30 | 7fffb2b67e30
4005aa | pop rbx | 109 | * | 0 | a
4005ab | pop rbp | 109 | * | 0 | 22
4005ac | ret  | 109 | -> | 40059a | 4005cb

## How to Use?

pin-instat works on both x32 and x64. I have tested it with gcc 4.8.2 (Fedora 19 x64) and Microsoft Visual C++ 2010 Express (Windows 7 x32), but it probably also works on all operating systems supported by PIN. First, you need to download PIN from intel.com. I used v2.13, but other version probably works as well.

For Linux build:

1. Copy `make.example` to `make.sh` and make your local changes.
2. Run `bash make.sh`.
3. The pin tool will be written to `obj-intel64/instat.so` for x64.
4. To profile a program, say `ls`, run `PINDIR/pin.sh -t obj-intel64/instat.so -- ls`

For Windows build:

1. Copy `makefile.nmake.sample` to `makefile.nmake`.
2. Open a MSVC command prompt (from start menu) and run `nmake -f makefile.nmake`.
3. The pin tool will be `instat.dll`.
4. To profile a program, say `notepad.exe`, run `PINDIR\pin.bat -t TOOLDIR\instat.dll -- C:\windows\notepad.exe`. The output will be saved only after notepad exits.
