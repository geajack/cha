import tokenize

with tokenize.open("example.py") as f:
    tokens = tokenize.generate_tokens(f.readline)
    for token in tokens:
        print(token)