# Coding Conventions

### C++ Naming Conventions

- Interface names (classes containing only pure virtual methods with no implementation) start with `I`, as in C#.
- Class names and public method names use PascalCase.
- Private method and field names use camelCase.
- Function argument names use camelCase.
- Template parameter names follow the C# style: `TKey`, `TValue`, `TIterator`.

### Comments

- Comment **why, not what**. Code describes the mechanism; comments explain intent, rationale, constraints, and non-obvious invariants.
- Each hand-encoded byte sequence, magic constant, hardware register offset, and ABI-boundary hack must have a one-line comment explaining the reason.
- Areas of technical debt point to [TechnicalDebt.md](TechnicalDebt.md).
- `#endif` and closing namespace braces may include trailing comments indicating their match.

### Git Commits

Follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) (`feat(...)`, `fix(...)`, `chore(...)`, `docs(...)`, etc.). The subject line is at most 72 characters; the commit body explains why.

### Assets and Images

Do not add binary image files to the repository. Reference external links or issue discussions when visual evidence is necessary.