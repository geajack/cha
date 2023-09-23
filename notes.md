A program is a sequence of statements. A statement can be either:

- Statement chain
- Code block
- Host statement
- Language statement

---

# Language

A program is a sequence of statements. A statement is either:

- A host statement chain.
- A language statement (beginning with a keyword).
- A codeblock, surrounded with curly braces.

A host statement is a sequence of simple host statements, separated by either >, |, >> or ||.

A > or a >> is always followed by a filename or filepath. A filename or filepath is a simple host token (i.e. a non-array variable or an unquoted string with no asterisks in it).

Rules for host statement chains:

    1. The first host statement in a chain of host statements is always a program invocation.
    2. A statement following a >, >> or >>> is always a file reference.
    3. A statement following a |, || or ||| is always a program invocation.
    3. The upstream of any chaining operator is always the most recent program invocation before it.
    4. The downstream of any chaining operator is always the statement immediately after it.

A ... indicates to ignore the previous newline (statement continuation)

It would be useful to be able to pipe stdout directly into a variable like `command | set var`, and pipe strings directly into stdout with `print "string" | command`

The most straightforward way to do this is to just let the user chain together arbitrary statements (except file references), and give those statements stdins and stdouts. Rules for language statements:
- the `error` command outputs to stderr
- the `print` command outputs to stdout
- `print` and `error` can accept a stream as an argument as well as other data types
- maybe `set` passes through its input?
- `call` produces the combined output of the function you're calling, i.e. all the `print`s and `error`s and everything else

Control flow:

- if, else if, else
- for ... in (iteration over arrays)
- while

Datatypes:

- Numeric
- Boolean
- String
- Array
- Stream?

Initial keywords:

- if, while, for
- set
- env
- return
- exit
- call
- fork
- 

Variables and envvars are not the same thing. Both are limited to the scope of the nearest codeblock, which you can use to set a bunch of envvars for the next program invocation:

```
{
    env PATH=.
    env CONFIG=.
    some-program
}
```

## Argument substitution
A program invocation is a space separated list of tokens. If a token begins with $, it must be of the form $variable_name. An array variable name is replaced with the list of strings inside it. Any other variable name is replaced with its string value. First, all non-variable tokens are *expanded*. If the token contains a ~, that ~ is replaced with /home/user. If the token contains any asterisks, it is subsituted for an array of strings according to standard glob rules. 