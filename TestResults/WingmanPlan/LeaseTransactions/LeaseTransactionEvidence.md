# Wingman Lease Transaction Evidence

Date: 2026-09-02 (Asia/Shanghai)

## Implemented contract

- Production lease maintenance is owned by one `AGuLiBattleGameState` 1.0 second repeating timer. Per-frame relay transport work processes only carrier/fragment deadlines.
- Exact server tuning is locked to Initial Candidate 3s, Offer Ready 3s, Baseline ACK 3s, Takeover Candidate 3s, Offer-to-completion Overall 10s, and Active/Stale/Unavailable/Revoke freshness boundaries 1s/2s/3s.
- Pending Offer preview is independent of the old Active lease freshness clock. Ready, ACK, and Batch ingress reject at their own deadline before mutating state.
- Resume keeps the same lease and original freshness/revoke clock; Takeover uses its own ACK/Candidate deadlines capped by the original Overall deadline.
- Death/Roster revision cancels an incomplete assembler and re-freezes the appropriate Bootstrap/Resume/Takeover cut. Deferred replenishment releases its stable ScheduleId exactly once after Active resumes.
- Active replenishment uses a dedicated non-atomic six-scope roster ACK barrier. It preserves Availability, accepted poses, sequence high-water marks, and freshness; after ACK only the affected Flight needs an ordinary Candidate.
- Disconnect retains the authoritative group and uses Offer -> Ready -> Commit -> exact baseline ACK -> atomic Takeover. Exhausted candidates enter retained NoOwner/Unavailable.
- Lease audit records carry theoretical deadline, detection time, and detection lag. The in-memory audit is bounded to 256 entries.
- Server Wingman movement-write counter remains zero in all transaction tests.

## Build evidence

- `GuLiStrikeEditor Win64 DebugGame`: PASS after the final Lease/Resume/Active-roster integration changes.
- `GuLiStrike Win64 Development`: PASS, 56 build actions, including final `GuLiStrike.exe` link.

## Automation evidence

| Report | Passed | Failed | Warnings |
|---|---:|---:|---:|
| `LeaseTransactions/index.json` | 7 | 0 | 0 |
| `LeaseRelayRegression/index.json` | 19 | 0 | 0 |
| `LeaseCombatLifecycle/index.json` | 1 | 0 | 0 |
| `LeaseProtocolV7/index.json` | 3 | 0 | 0 |

The Protocol v7 suite includes the locked Candidate Golden Bytes test; no Candidate wire-byte change was introduced by the Lease state work.

## Evidence boundary

These are deterministic Unreal Automation/core and in-process integration results. They are not real socket, multiprocess Listen, or source-engine Dedicated Server evidence. The separately owned `LeaseNetworkAcceptance` weak-network fixture was still on the former sub-second/atomic-first Resume ordering at this evidence cut and is intentionally not counted as passing here; it must be adapted and rerun by its owning network acceptance task before a network gate is claimed.
