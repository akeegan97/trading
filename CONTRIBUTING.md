# Contributing

## Branch Strategy

1. Never commit directly to `main`.
2. Create short-lived branches from `main`:
   - `feat/<name>` for features
   - `fix/<name>` for bug fixes
   - `chore/<name>` for maintenance
3. Keep PRs focused and small enough to review quickly.

## Pull Request Rules

1. Open a PR early (draft PR is fine).
2. Keep the PR description updated.
3. Link related issues in the PR description.
4. Ensure CI passes before requesting review.
5. If this is a solo repo, approval is optional.
6. If collaborators are added, require at least one approval before merge.
7. Resolve all review comments before merge.

## Merge Rules

1. Use **Squash and merge** unless there is a specific reason not to.
2. Delete branch after merge.
3. Never bypass required checks.

## Commit Messages

Use clear commit prefixes when possible:

- `feat: ...`
- `fix: ...`
- `chore: ...`
- `docs: ...`
- `test: ...`

## Tooling Expectations

1. C++:
   - Build with CMake
   - Keep code formatted via `.clang-format`
   - Keep static analysis clean via `.clang-tidy`
2. Python:
   - Keep lint clean via `ruff`
   - Keep tests passing via `pytest`
