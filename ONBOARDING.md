# Fresh-machine onboarding

This is the canonical bootstrap entry point for a new workstation or a fresh coding-agent environment. Complete this document before implementation work. The objective is a reproducible machine that can build, test, inspect, and operate this repository without rediscovering tooling mid-campaign.

## 1. Preflight rule

1. Clone the repository and enter its root.
2. Confirm the intended repository/branch and fetch current `origin/main`.
3. Read the repository control-plane documents before changing code: `AGENTS.md`, `AUTONOMOUS.md`, `CLAUDE.md`, `README.md`, `.agent/`, `progress.md`, active OpenSpec state.
4. Install/verify the machine prerequisites below.
5. Enable the committed agent integrations and repository-local skills.
6. Restore dependencies from lockfiles/pins; do not casually upgrade them during bootstrap.
7. Run the baseline validation commands.
8. Only then begin a development campaign. If a prerequisite cannot be satisfied, record it as an environment blocker rather than weakening a gate.

Credentials, API keys, signing material, account logins, licensed models/assets, and other secrets are machine/user responsibilities. Never commit them.

## 2. Supported host and prerequisites

**Primary host:** Windows 10 1809+ / Windows 11; native C++20/Win32 application.

**Required machine tools**
- Git
- Visual Studio 2022 Build Tools/IDE with MSVC C++20 and Windows SDK
- CMake + a supported generator (Ninja or Visual Studio)
- PowerShell
- Node.js/npm for repository-local MCP/OpenSpec tooling
- Python for the committed fetch MCP

**Task-dependent / optional tools**
- Windows service/elevation capability for privileged-index integration tests
- signing material only for real production release qualification


## 3. Agent setup

- Load repository instructions before acting. Prefer committed repository state over chat history.
- Repository-local skills: `goal`.
- Agent adapter/config directories present in this repository should be discovered and used in-place; do not duplicate them globally unless the harness cannot load repository-local configuration.
- Relevant committed agent surfaces: `.agent/`, `.agents/`, `.claude/`, `.cline/`, `.codex/`, `.cursor/`, `.gemini/`, `.kilocode/`, `.kimi*/`, `.opencode/`.
- MCP policy: Use committed `.mcp.json`: Playwright MCP, Context7, GitHub MCP (`GITHUB_PERSONAL_ACCESS_TOKEN` from environment only), and Python `mcp_server_fetch`. Do not commit PATs or broaden MCP authority.
- Keep MCP/plugin authority narrow. Documentation/diagnostic MCPs are not permission to change architecture, bypass tests, or publish.
- Authentication for GitHub and coding-agent CLIs is configured separately on the machine. Never write tokens into tracked files.

## 4. Bootstrap

Run the repository's pinned bootstrap, not an improvised dependency upgrade:

```powershell
cmake --version
cl
npm --version
# Configure using the repository's supported CMake preset/build procedure from README/CMakePresets.json
```

FastFiles has privileged/unprivileged process boundaries. Never bypass the service/engine/UI architecture or release signature checks just to make a new machine convenient.


## 5. Editor/LSP baseline

Prefer Visual Studio's C++ language service or clangd configured against CMake compile commands. CMake diagnostics and MSVC `/W4 /WX` are authoritative; keep Windows SDK symbols available.

The editor is optional; the language servers are not. Agents should have diagnostics/type information available before editing non-trivial code.

## 6. Baseline verification

```powershell
cmake --preset <supported-preset>
cmake --build --preset <supported-build-preset>
ctest --preset <supported-test-preset>
# Then run the repository's documented FastFiles verification scripts for the current campaign.
```

A fresh machine is considered **development-ready** only when the applicable non-external gates above pass. Hardware/device/signing/account gates may remain explicitly blocked if the repository already classifies them that way.

## 7. Fresh-agent instruction

Use this exact operating rule when handing the repository to a new agent:

> Read `ONBOARDING.md` first. Set up every applicable prerequisite, repository-local skill, MCP/plugin, dependency, browser/device/runtime tool, and validation gate described there. Then read the repository's durable agent state and only start implementation after preflight is green or a genuine environment blocker is recorded. Do not replace pinned tooling, skip gates, or invent work to compensate for a missing machine capability.
