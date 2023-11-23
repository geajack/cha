For embedding arguments, we could have:

$variablename
$(expression) (value becomes argument)
${code block} (stdout becomes argument)

This seems like it would parse easily and be unambiguous. I thought I needed to restrict myself to only embedding variable names to avoid bash craziness, but actually I think the reason bash arguments are so bad is because bash is bad in many other ways.

---

0: ROOT
1: CODEBLOCK
2: wc -l
3: CODEBLOCK prints
4: grep apple

0: no pipes
1: 0 -> 0x5198f0
2: 0x5198f0 -> 0
3: 0 -> 0x51a104
4: 0x51a104 -> 0

---

get a node of the AST

execute it
   -> it might suggest a next node where it wants the IP to go (other wise that's null)

did the instruction we just execute tell us where it wanted to go? if so, go there

else, if it's the root node we're done

else:
    go to its sibling, if that's null go to the parent, etc

if statement has opinion
{
    go there
}
else while not gone anywhere
{
    if current == root
    {
        stop
    }
    else
    {
        if (current->next_sibling)
        {
            go to next sibling
        }
        else
        {
            current = parent
        }

        if current is a while loop
        {
            go to current
        }
    }
}

next = 0

if statement has opinion:
    next = where statement wants to go    

node = current
while havent found next:
    if node is root:
        STOP
    else if node->next_sibling
        next = node->next_sibling        
    else:
        if node->parent is while loop:
            next = node->parent
        else:
            node = node->parent

---

stack of siblings:
- if 1
- if 2
- if 3
- if 4

state = just opened if statement

initial stack: [program->first_child]

push regular statement:
  p = pop  
  *p = self
  if not just opened if statement:
    push self->next_sibling

push if statement:
  p = pop
  *p = self
  if not just opened if statement:
    push self->next_sibling
  push self->condition->next_sibling
  set "just pushed if statement"

open codeblock:
  p = pop
  *p = self
  if not just opened if statement:
    push self->next_sibling
  push self->first_child


close codeblock:
  pop

---

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