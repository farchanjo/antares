---
kind: discipline
evidence: quality
fingerprint: 995d54fd1b812740
sources: doc/arch/speckit.toml
---
# review skill

Family: `discipline`.

## Coverage

- `doc/arch/quality`

## Commands and APIs

- see `doc/arch/speckit.toml`

## Conventions

- Follow the corpus rule the command comes from; quote it, never paraphrase.
- Keep the invocation identical to the one the gate runs.
- Pin every version you name.

## Pitfalls

- Running the command from the wrong root.
- Trusting first cached result after changing the input.

## Checklist

- The command exists in the binary or the lockfile.
- The input the rule names is the input you fed it.
- The output is the one the gate expects.

## Customized

<!-- customized:start -->

<!-- customized:end -->
