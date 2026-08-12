# Antigravity Kimi CLI Parity Guidelines

This configuration ensures Antigravity operates with capability and workflow parity matching Kimi Code CLI in this repository.

## Capabilities & Integrations

1. **OpenSpec Specification-Driven Workflow**
   - Access to all 6 OpenSpec skills (`openspec-propose`, `openspec-apply-change`, `openspec-explore`, `openspec-update-change`, `openspec-sync-specs`, `openspec-archive-change`).
   - Standard slash command equivalents: `/opsx:propose`, `/opsx:apply`, `/opsx:explore`, `/opsx:update`, `/opsx:sync`, `/opsx:archive`.

2. **MCP Tool Integration**
   - **Playwright MCP**: Browser automation & UI testing with unrestricted file access.
   - **Upstash Context7 MCP**: Live documentation query and library ID resolution.
   - **GitHub MCP**: Repository search, issue management, code search, and PR creation.
   - **Fetch MCP**: Web content extraction and HTTP endpoint interaction.

3. **Autonomous Execution & Loop Control**
   - Integration with the repository's autonomous loop (`verify/intake.ps1 autonomous` and `AGENTS.md` rules).
   - Support for background task execution (`run_command` async, `manage_task`, `schedule`) with non-blocking execution models.
   - Subagent task delegation using `invoke_subagent` and `define_subagent`.

4. **Security & Repository Invariants**
   - Adhere strictly to the three-process Windows privilege split (`FastFilesIndexSvc`, `FastFilesEngine`, `FastFiles`).
   - Obey security invariants: closed command protocol, opaque handles, symmetric mutual auth, frame/parser hardening, and strict compiler options.
