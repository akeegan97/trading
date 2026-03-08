# Trading Repo

This repository is set up for a PR-driven workflow with automated CI checks for C++ (CMake + clang) and Python.

Checks should run in both places:
- Locally before pushing (fast feedback)
- In GitHub Actions on every PR (merge gate and audit trail)

Performance optimization ideas tracked here:
- [`docs/perf_backlog.md`](docs/perf_backlog.md)

Pipeline handoff contract is documented here:
- [`docs/data_contract.md`](docs/data_contract.md)

Trader runtime config drop-file example:
- [`docs/trader_config.example.json`](docs/trader_config.example.json)

## Workflow

1. Create a branch from `main`:
   - `feat/<short-description>`
   - `fix/<short-description>`
   - `chore/<short-description>`
2. Push your branch and open a pull request.
3. Wait for CI to pass.
4. Merge via **Squash and merge**.

Direct pushes to `main` should be blocked by branch protection.

## GitHub Setup Checklist

After pushing this repo to GitHub, configure:

1. Set default branch to `main`.
2. Enable branch protection on `main`:
   - Require a pull request before merging
   - For solo use: approvals optional (set required approvals to 0)
   - For team use: require at least 1 approval
   - Dismiss stale approvals when new commits are pushed
   - Optional for solo use: Require review from Code Owners
   - Require status checks to pass before merging
   - Required checks: select the check names shown after the first CI run. Recommended:
     - `CI / Baseline checks`
     - `CI / C++ clang-format`
     - `CI / C++ CMake build + clang-tidy + tests` (when using CMake)
     - `CI / Python ruff + pytest` (when using Python)
   - Require conversation resolution before merging
   - Include administrators
   - Disable force pushes and deletions
3. In repository settings, disable merge methods you do not want (recommended: keep only Squash merge).

## Local Commands (Match CI)

```bash
# C++ format check
clang-format --dry-run --Werror $(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx')

# C++ configure/build/test via presets
cmake --preset ci
cmake --build --preset build-ci --parallel
ctest --preset test-ci

# C++ clang-tidy
clang-tidy -p build/ci --warnings-as-errors='*' $(git ls-files '*.cc' '*.cpp' '*.cxx')

# Python lint/test
ruff check .
pytest -q
```

## vcpkg Guidance

Use `vcpkg` only when you have non-trivial C++ dependencies to manage across machines/CI.

- If dependencies are minimal or system-provided: skip `vcpkg` for now.
- If you add multiple third-party C++ libs: use `vcpkg` manifest mode.

This repo includes optional presets (`dev-vcpkg`, `rel-vcpkg`, `ci-vcpkg`) that use
`$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`.

## First Push

```bash
git add .
git commit -m "chore: bootstrap repo standards"
git remote add origin <your-github-repo-url>
git push -u origin main
```

## Running Trader App With Drop File

```bash
# pass file path directly
./build/dev/cpp/trader_app docs/trader_config.example.json

# or by env var
TRADING_CONFIG_PATH=docs/trader_config.example.json ./build/dev/cpp/trader_app
```
