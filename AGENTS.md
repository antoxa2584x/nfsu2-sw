# Repository rules — rights, attribution, and public content

**Mandatory for every contributor, maintainer, automation, AI assistant, commit,
pull request, build, release, and Store/catalog submission.**

## 1. Ownership and licenses are separate

- The repository owner maintains **this fork/port and their own contributions**;
  ownership of a GitHub repository, title ID, release, or Store listing **does
  not mean ownership of the upstream engine, game, artwork, or dependencies**.
- Each upstream author retains their copyright and applicable license. Keep
  upstream copyright notices, SPDX headers, attribution, source offers, and
  license texts intact. Never replace, remove, narrow, or relicense those
  notices to suggest the fork owner authored the upstream work.
- Do **not** claim that an MIT/GPL/MPL/CC0 license applies to an entire mixed
  program merely because one component carries it. Record the exact upstream
  repository, pinned commit, relevant files, license text, and any exceptions.
- Previously granted licenses remain relevant; changing a repository license
  label cannot retroactively cancel another author's grant or grant rights to
  code not covered by it. No guessed ownership or permission.
- New, independently authored code belongs to its actual rightsholder(s).
  **Do not automatically put MIT, GPL, CC0, or any other public license on new
  code.** Follow the rightsholder's explicit decision while respecting any
  applicable obligations of incorporated third-party material. Never remove
  existing valid licenses from pre-existing files.
- A missing, unclear, disputed, or incompatible permission is a **release
  blocker**. Investigate and document it privately; obtain a valid grant or
  replace/remove the affected implementation. A repository being public is not
  evidence of permission.

## 2. Public repository — strict minimum

- Commit only source, build instructions needed by actual users, required
  notices, reproducible tests, and information necessary for public use or
  contribution. Keep README factual, neutral, short, and current.
- **Do not publish internal planning, research notebooks, private status
  reports, speculative milestones, incident narratives, operator handoffs,
  prompts, scratchpads, diagnostic receipts, local infrastructure details, or
  personal correspondence.** Keep them outside the public repository and all
  tracked directories. Do not invent an internal documentation directory in Git.
- Never commit credentials, secrets, API tokens/keys, passwords, session data,
  private keys or signing material, production configuration, environment
  files, private paths, user identifiers, logs, crash dumps, ISO/ROM/game
  assets, extracted game material, saves, generated game modules, or user data.
  Public verification keys and deliberately public sample configuration may be
  retained only after verifying they contain no secrets or private data.
- Use ignored local/private storage for development-only materials; review
  every staged diff, generated file, archive, release asset, and CI artifact
  before publication. Secrets must come from appropriate secret stores, not
  tracked configuration or Markdown.
- If sensitive information may already have been published: immediately stop
  distribution, revoke/rotate affected credentials, assess exposure (including
  Git history, forks, caches, tags, artifacts, and logs), and follow a
  controlled remediation process. Deleting a file in a later commit is **not**
  sufficient to erase its prior publication.

## 3. Release gate and accurate representation

- Require an actual source/dependency/license inventory and review of the
  final compiled/linked/package contents; do not equate successful compilation
  with redistribution rights or gameplay verification.
- Preserve applicable copyleft obligations and original upstream notices.
  Never describe upstream work as solely owned by the fork maintainer.
- Store submissions must say **"maintainer of this port/repository and submitter
  of this listing"**, not "owner of the original engine/application", unless
  separately established. Distinguish operator, original authors, copyright
  holders, and license grantors.
- Where rights, origin, sensitive content, or license compatibility cannot be
  established: **do not publish, package, merge, or submit a release**.
- Do not bypass these rules to meet a deadline. This document is operational
  guidance only: it does not itself license, transfer, or revoke any copyright.

## 4. Project-specific known rights (not a complete legal audit)

- Contribution fork of `antoxa2584x/nfsu2-sw`. PAL/EU development does not confer copyright over the original Xbox-derived engine.
- No general root LICENSE was verified in upstream or fork; **distribution status is unresolved** pending per-file permission, contribution and third-party dependency review. Do not relicense or republish based solely on public source visibility.
- Game executables/assets and Electronic Arts rights are outside the fork's grants.
